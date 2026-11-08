# ChromiumOS builds C++ without exceptions (make.conf.common-target). Packages
# mapped to this file in profiles/base/package.bashrc need them, as
# chromiumos-overlay's own <package>.bashrc files do for theirs.
cros_pre_src_prepare_nickel_cxx_exceptions() {
	cros_enable_cxx_exceptions
}
