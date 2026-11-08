// Distributed under the terms of the GNU General Public License v2
//
// Picture previews for Chrome (the desktop's icons, Files' thumbnails) of
// what Chrome can't decode itself: HEIC/HEIF photos, H.265 (HEVC) videos and
// other video formats. Chrome asks Thumbnail(path, size).
//
// Thumbnails follow the freedesktop Thumbnail Managing Standard, so Linux
// apps' file managers share them: ~/.cache/thumbnails/<normal|large|x-large|
// xx-large>/<MD5 of the file's URI>.png, with Thumb::URI and Thumb::MTime; one
// whose Thumb::MTime is not the file's is made again. They are made by the
// thumbnailers in <data dir>/thumbnailers/*.thumbnailer (libheif's
// heif-thumbnailer, ffmpegthumbnailer), as GNOME's file manager runs them:
// inside bubblewrap, with no network and no other process in view, the file
// read-only and an empty directory for the output the only writable place.
// Two run at once at most; asks for the same thumbnail share one run. A file
// that could not be done is remembered (fail/nickel-appd/) until it changes.

#include "thumbnail.h"

#include <glib/gstdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <zlib.h>

#include <deque>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr guint kMaxRunning = 2;
constexpr guint kTimeoutSeconds = 30;
constexpr char kBubblewrap[] = "/usr/bin/bwrap";
constexpr char kGroup[] = "Thumbnailer Entry";

struct Thumbnailer {
  std::string name;
  std::string exec;
  std::vector<std::string> mime_types;
};

}  // namespace

struct ThumbnailJob;

struct Thumbnails {
  std::vector<Thumbnailer> thumbnailers;
  // Jobs waiting or running, by their cache file.
  std::map<std::string, ThumbnailJob*> jobs;
  std::deque<ThumbnailJob*> waiting;
  guint running = 0;
};

struct ThumbnailJob {
  Thumbnails* thumbnails = nullptr;
  std::vector<GDBusMethodInvocation*> invocations;
  // The file as asked for (its URI names the thumbnail) and as it really is
  // (what the thumbnailer gets).
  std::string uri;
  std::string real_path;
  std::string mtime;
  guint pixels = 0;
  std::string cache_path;
  std::string fail_path;
  const Thumbnailer* thumbnailer = nullptr;
  bool started = false;
  std::string out_dir;
  GSubprocess* process = nullptr;
  guint timeout = 0;
};

namespace {

std::string CacheDir() {
  return std::string(g_get_user_cache_dir()) + "/thumbnails";
}

// The standard's size directory for |size| pixels, and its size.
const char* SizeDir(guint size, guint* pixels) {
  if (size <= 128) {
    *pixels = 128;
    return "normal";
  }
  if (size <= 256) {
    *pixels = 256;
    return "large";
  }
  if (size <= 512) {
    *pixels = 512;
    return "x-large";
  }
  *pixels = 1024;
  return "xx-large";
}

guint32 BigEndian(const std::string& bytes, size_t at) {
  return static_cast<guint32>(static_cast<guint8>(bytes[at])) << 24 |
         static_cast<guint32>(static_cast<guint8>(bytes[at + 1])) << 16 |
         static_cast<guint32>(static_cast<guint8>(bytes[at + 2])) << 8 |
         static_cast<guint32>(static_cast<guint8>(bytes[at + 3]));
}

void AppendBigEndian(std::string* bytes, guint32 value) {
  for (int shift = 24; shift >= 0; shift -= 8) {
    bytes->push_back(static_cast<char>((value >> shift) & 0xff));
  }
}

// A PNG's chunks (type, data) in order, or false if it isn't a whole PNG.
bool PngChunks(const std::string& png,
               std::vector<std::pair<std::string, std::string>>* chunks) {
  static constexpr char kSignature[] = "\x89PNG\r\n\x1a\n";
  if (png.size() < 8 || png.compare(0, 8, kSignature, 8) != 0) {
    return false;
  }
  size_t pos = 8;
  while (pos + 12 <= png.size()) {
    const guint32 length = BigEndian(png, pos);
    if (length > png.size() - pos - 12) {
      return false;
    }
    chunks->emplace_back(png.substr(pos + 4, 4), png.substr(pos + 8, length));
    if (chunks->back().first == "IEND") {
      return chunks->front().first == "IHDR";
    }
    pos += 12 + length;
  }
  return false;
}

std::string TextKey(const std::string& data) {
  return data.substr(0, data.find('\0'));
}

std::string TextValue(const std::string& data) {
  const size_t nul = data.find('\0');
  return nul == std::string::npos ? "" : data.substr(nul + 1);
}

void AppendChunk(std::string* png,
                 const std::string& type,
                 const std::string& data) {
  AppendBigEndian(png, static_cast<guint32>(data.size()));
  const size_t start = png->size();
  *png += type;
  *png += data;
  const uLong crc =
      crc32(crc32(0L, Z_NULL, 0),
            reinterpret_cast<const Bytef*>(png->data() + start),
            static_cast<uInt>(4 + data.size()));
  AppendBigEndian(png, static_cast<guint32>(crc));
}

// |png| with the standard's Thumb::URI and Thumb::MTime (and Software) right
// after its header, in place of any it had; "" if it isn't a PNG.
std::string WithThumbText(const std::string& png, const ThumbnailJob& job) {
  std::vector<std::pair<std::string, std::string>> chunks;
  if (!PngChunks(png, &chunks)) {
    return "";
  }
  std::string out(png, 0, 8);
  for (const auto& [type, data] : chunks) {
    if (type == "tEXt") {
      const std::string key = TextKey(data);
      if (key == "Thumb::URI" || key == "Thumb::MTime" || key == "Software") {
        continue;
      }
    }
    AppendChunk(&out, type, data);
    if (type == "IHDR") {
      AppendChunk(&out, "tEXt", std::string("Thumb::URI") + '\0' + job.uri);
      AppendChunk(&out, "tEXt",
                  std::string("Thumb::MTime") + '\0' + job.mtime);
      AppendChunk(&out, "tEXt", std::string("Software") + '\0' + "nickel-appd");
    }
  }
  return out;
}

// The cached thumbnail, if it is there and is of the file as it is now.
bool ReadCached(const ThumbnailJob& job, std::string* png) {
  g_autofree gchar* contents = nullptr;
  gsize length = 0;
  if (!g_file_get_contents(job.cache_path.c_str(), &contents, &length,
                           nullptr)) {
    return false;
  }
  std::string bytes(contents, length);
  std::vector<std::pair<std::string, std::string>> chunks;
  if (!PngChunks(bytes, &chunks)) {
    return false;
  }
  std::string uri, mtime;
  for (const auto& [type, data] : chunks) {
    if (type != "tEXt") {
      continue;
    }
    if (TextKey(data) == "Thumb::URI") {
      uri = TextValue(data);
    } else if (TextKey(data) == "Thumb::MTime") {
      mtime = TextValue(data);
    }
  }
  if (uri != job.uri || mtime != job.mtime) {
    return false;
  }
  *png = std::move(bytes);
  return true;
}

void ReplyPng(GDBusMethodInvocation* invocation, const std::string& png) {
  GVariant* bytes = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, png.data(),
                                              png.size(), 1);
  g_dbus_method_invocation_return_value(invocation,
                                        g_variant_new("(@ay)", bytes));
}

void ReplyNone(GDBusMethodInvocation* invocation, const char* why) {
  g_dbus_method_invocation_return_error_literal(
      invocation, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, why);
}

void LoadThumbnailers(Thumbnails* thumbnails) {
  for (const gchar* const* data_dir = g_get_system_data_dirs(); *data_dir;
       ++data_dir) {
    g_autofree gchar* dir_path =
        g_build_filename(*data_dir, "thumbnailers", nullptr);
    GDir* dir = g_dir_open(dir_path, 0, nullptr);
    if (!dir) {
      continue;
    }
    while (const gchar* name = g_dir_read_name(dir)) {
      if (!g_str_has_suffix(name, ".thumbnailer")) {
        continue;
      }
      g_autofree gchar* path = g_build_filename(dir_path, name, nullptr);
      g_autoptr(GKeyFile) key_file = g_key_file_new();
      if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE,
                                     nullptr)) {
        continue;
      }
      g_autofree gchar* try_exec =
          g_key_file_get_string(key_file, kGroup, "TryExec", nullptr);
      if (try_exec) {
        g_autofree gchar* found = g_find_program_in_path(try_exec);
        if (!found) {
          continue;
        }
      }
      g_autofree gchar* exec =
          g_key_file_get_string(key_file, kGroup, "Exec", nullptr);
      g_auto(GStrv) types = g_key_file_get_string_list(
          key_file, kGroup, "MimeType", nullptr, nullptr);
      if (!exec || !types) {
        continue;
      }
      Thumbnailer thumbnailer;
      thumbnailer.name = name;
      thumbnailer.exec = exec;
      for (gchar** type = types; *type; ++type) {
        thumbnailer.mime_types.emplace_back(*type);
      }
      syslog(LOG_INFO, "thumbnail: %s, for %zu types", name,
             thumbnailer.mime_types.size());
      thumbnails->thumbnailers.push_back(std::move(thumbnailer));
    }
    g_dir_close(dir);
  }
}

const Thumbnailer* FindThumbnailer(const Thumbnails* thumbnails,
                                   const char* content_type) {
  for (const Thumbnailer& thumbnailer : thumbnails->thumbnailers) {
    for (const std::string& type : thumbnailer.mime_types) {
      if (g_content_type_equals(content_type, type.c_str())) {
        return &thumbnailer;
      }
    }
  }
  for (const Thumbnailer& thumbnailer : thumbnails->thumbnailers) {
    for (const std::string& type : thumbnailer.mime_types) {
      if (g_content_type_is_a(content_type, type.c_str())) {
        return &thumbnailer;
      }
    }
  }
  return nullptr;
}

// The thumbnailer's Exec, its % codes filled in, inside bubblewrap.
bool Command(const ThumbnailJob& job,
             const std::string& out,
             std::vector<std::string>* argv) {
  gint argc = 0;
  g_auto(GStrv) exec_argv = nullptr;
  if (!g_shell_parse_argv(job.thumbnailer->exec.c_str(), &argc, &exec_argv,
                          nullptr)) {
    return false;
  }
  *argv = {
      kBubblewrap,
      "--ro-bind", "/usr", "/usr",
      "--ro-bind-try", "/lib", "/lib",
      "--ro-bind-try", "/lib64", "/lib64",
      "--ro-bind-try", "/bin", "/bin",
      "--ro-bind-try", "/etc/ld.so.cache", "/etc/ld.so.cache",
      "--proc", "/proc",
      "--dev", "/dev",
      "--tmpfs", "/tmp",
      "--ro-bind", job.real_path, job.real_path,
      "--bind", job.out_dir, job.out_dir,
      "--unshare-all",
      "--die-with-parent",
      "--new-session",
      "--cap-drop", "ALL",
      "--clearenv",
      "--setenv", "PATH", "/usr/bin:/bin",
      "--setenv", "HOME", "/tmp",
      "--",
  };
  const std::string size = std::to_string(job.pixels);
  for (gint i = 0; i < argc; ++i) {
    const std::string arg = exec_argv[i];
    std::string filled;
    for (size_t pos = 0; pos < arg.size(); ++pos) {
      if (arg[pos] != '%' || pos + 1 == arg.size()) {
        filled += arg[pos];
        continue;
      }
      switch (arg[++pos]) {
        case 'i':
          filled += job.real_path;
          break;
        case 'u':
          filled += job.uri;
          break;
        case 'o':
          filled += out;
          break;
        case 's':
          filled += size;
          break;
        case '%':
          filled += '%';
          break;
        default:
          filled += '%';
          filled += arg[pos];
      }
    }
    argv->push_back(filled);
  }
  return true;
}

void StartJob(ThumbnailJob* job);

void StartWaiting(Thumbnails* thumbnails) {
  while (thumbnails->running < kMaxRunning && !thumbnails->waiting.empty()) {
    ThumbnailJob* job = thumbnails->waiting.front();
    thumbnails->waiting.pop_front();
    StartJob(job);
  }
}

// Saves the thumbnail (or that there is none), answers everyone who asked,
// and starts the next job.
void Finish(ThumbnailJob* job, const std::string& png) {
  if (!png.empty()) {
    g_autofree gchar* dir = g_path_get_dirname(job->cache_path.c_str());
    g_mkdir_with_parents(dir, 0700);
    g_file_set_contents_full(job->cache_path.c_str(), png.data(), png.size(),
                             G_FILE_SET_CONTENTS_CONSISTENT, 0600, nullptr);
  } else {
    g_autofree gchar* dir = g_path_get_dirname(job->fail_path.c_str());
    g_mkdir_with_parents(dir, 0700);
    g_file_set_contents_full(job->fail_path.c_str(), job->mtime.data(),
                             job->mtime.size(), G_FILE_SET_CONTENTS_CONSISTENT,
                             0600, nullptr);
  }
  for (GDBusMethodInvocation* invocation : job->invocations) {
    if (png.empty()) {
      ReplyNone(invocation, "no thumbnail");
    } else {
      ReplyPng(invocation, png);
    }
  }
  if (!job->out_dir.empty()) {
    g_unlink((job->out_dir + "/thumbnail.png").c_str());
    g_rmdir(job->out_dir.c_str());
  }
  g_clear_object(&job->process);
  Thumbnails* thumbnails = job->thumbnails;
  thumbnails->jobs.erase(job->cache_path);
  if (job->started) {
    thumbnails->running--;
  }
  delete job;
  StartWaiting(thumbnails);
}

gboolean OnJobTimeout(gpointer data) {
  ThumbnailJob* job = static_cast<ThumbnailJob*>(data);
  job->timeout = 0;
  syslog(LOG_WARNING, "thumbnail: %s took over %us on %s, stopped",
         job->thumbnailer->name.c_str(), kTimeoutSeconds, job->uri.c_str());
  g_subprocess_force_exit(job->process);
  return G_SOURCE_REMOVE;
}

void OnJobExited(GObject*, GAsyncResult* result, gpointer data) {
  ThumbnailJob* job = static_cast<ThumbnailJob*>(data);
  g_subprocess_wait_finish(job->process, result, nullptr);
  if (job->timeout) {
    g_source_remove(job->timeout);
    job->timeout = 0;
  }
  std::string png;
  if (g_subprocess_get_if_exited(job->process) &&
      g_subprocess_get_exit_status(job->process) == 0) {
    g_autofree gchar* contents = nullptr;
    gsize length = 0;
    if (g_file_get_contents((job->out_dir + "/thumbnail.png").c_str(),
                            &contents, &length, nullptr)) {
      png = WithThumbText(std::string(contents, length), *job);
    }
  }
  if (png.empty()) {
    syslog(LOG_WARNING, "thumbnail: %s made none of %s",
           job->thumbnailer->name.c_str(), job->uri.c_str());
  }
  Finish(job, png);
}

void StartJob(ThumbnailJob* job) {
  job->started = true;
  job->thumbnails->running++;
  g_autofree gchar* dir = g_dir_make_tmp("nickel-thumbnail-XXXXXX", nullptr);
  std::vector<std::string> args;
  if (!dir) {
    Finish(job, "");
    return;
  }
  job->out_dir = dir;
  if (!Command(*job, job->out_dir + "/thumbnail.png", &args)) {
    Finish(job, "");
    return;
  }
  std::vector<const gchar*> argv;
  for (const std::string& arg : args) {
    argv.push_back(arg.c_str());
  }
  argv.push_back(nullptr);
  g_autoptr(GError) error = nullptr;
  job->process = g_subprocess_newv(
      argv.data(),
      static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                    G_SUBPROCESS_FLAGS_STDERR_SILENCE),
      &error);
  if (!job->process) {
    syslog(LOG_WARNING, "thumbnail: cannot run %s: %s",
           job->thumbnailer->name.c_str(), error->message);
    Finish(job, "");
    return;
  }
  job->timeout = g_timeout_add_seconds(kTimeoutSeconds, OnJobTimeout, job);
  g_subprocess_wait_async(job->process, nullptr, OnJobExited, job);
}

}  // namespace

Thumbnails* ThumbnailsNew() {
  Thumbnails* thumbnails = new Thumbnails;
  LoadThumbnailers(thumbnails);
  return thumbnails;
}

void HandleThumbnail(Thumbnails* thumbnails,
                     GDBusMethodInvocation* invocation,
                     GVariant* parameters) {
  const gchar* path = nullptr;
  guint32 size = 0;
  g_variant_get(parameters, "(&su)", &path, &size);
  if (!g_path_is_absolute(path) || size == 0) {
    g_dbus_method_invocation_return_error_literal(
        invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
        "expected an absolute path and a size");
    return;
  }
  g_autofree gchar* real_path = realpath(path, nullptr);
  g_autoptr(GFile) file = g_file_new_for_path(real_path ? real_path : path);
  g_autoptr(GFileInfo) info = g_file_query_info(
      file,
      G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE
      "," G_FILE_ATTRIBUTE_TIME_MODIFIED,
      G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
  if (!real_path || !info ||
      g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR) {
    ReplyNone(invocation, "not a file");
    return;
  }

  ThumbnailJob* job = new ThumbnailJob;
  job->thumbnails = thumbnails;
  job->real_path = real_path;
  g_autofree gchar* uri = g_filename_to_uri(path, nullptr, nullptr);
  job->uri = uri ? uri : "";
  job->mtime = std::to_string(
      g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED));
  g_autofree gchar* md5 =
      g_compute_checksum_for_string(G_CHECKSUM_MD5, job->uri.c_str(), -1);
  const char* size_dir = SizeDir(size, &job->pixels);
  job->cache_path = CacheDir() + "/" + size_dir + "/" + md5 + ".png";
  job->fail_path = CacheDir() + "/fail/nickel-appd/" + md5 + ".png";

  std::string png;
  if (ReadCached(*job, &png)) {
    ReplyPng(invocation, png);
    delete job;
    return;
  }
  g_autofree gchar* failed_mtime = nullptr;
  if (g_file_get_contents(job->fail_path.c_str(), &failed_mtime, nullptr,
                          nullptr) &&
      job->mtime == failed_mtime) {
    ReplyNone(invocation, "no thumbnail");
    delete job;
    return;
  }
  const char* content_type = g_file_info_get_content_type(info);
  job->thumbnailer =
      content_type ? FindThumbnailer(thumbnails, content_type) : nullptr;
  if (!job->thumbnailer) {
    ReplyNone(invocation, "no thumbnailer for this type");
    delete job;
    return;
  }

  auto pending = thumbnails->jobs.find(job->cache_path);
  if (pending != thumbnails->jobs.end()) {
    pending->second->invocations.push_back(invocation);
    delete job;
    return;
  }
  job->invocations.push_back(invocation);
  thumbnails->jobs[job->cache_path] = job;
  if (thumbnails->running < kMaxRunning) {
    StartJob(job);
  } else {
    thumbnails->waiting.push_back(job);
  }
}
