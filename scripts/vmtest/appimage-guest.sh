# Runs as the session user (apps-test.sh feeds it to nickel-session-exec sh -s).
cd || exit 1
mkdir -p Applications && cd Applications || exit 1
curl -fsSLo appimagetool.AppImage \
  https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
chmod +x appimagetool.AppImage
echo "appimagetool: $(./appimagetool.AppImage --version 2>&1 | tail -1)"
url=$(curl -fsSL https://api.github.com/repos/obsidianmd/obsidian-releases/releases?per_page=5 |
  grep -oE '"browser_download_url": *"[^"]*/Obsidian-[0-9.]+\.AppImage"' | head -1 |
  sed 's/.*"\(https[^"]*\)"/\1/')
echo "obsidian: $url"
curl -fsSLo Obsidian.AppImage "$url" && chmod +x Obsidian.AppImage
nohup ./Obsidian.AppImage >/tmp/obsidian.log 2>&1 &
curl -fsSLo Etcher.AppImage \
  https://github.com/balena-io/etcher/releases/download/v1.7.9/balenaEtcher-1.7.9-x64.AppImage &&
  chmod +x Etcher.AppImage
nohup ./Etcher.AppImage >/tmp/etcher.log 2>&1 &
