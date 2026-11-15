# Distributed under the terms of the GNU General Public License v2
#
# NickelOS (docs/gentoo-at-root.md): before each package merges (binary ones
# too), what it would install goes where Gentoo's world has it on NickelOS:
#  - merged /usr: on a Gentoo at / of its own, /bin, /sbin and /usr/sbin
#    lead to /usr/bin, /lib and /lib64 to /usr/lib and /usr/lib64, and
#    packages install through those links. Here /bin, /sbin, /usr/sbin and
#    /lib are the OS's own directories, so their files go to /usr/bin and
#    /usr/lib (and Portage records those paths); linker scripts that name
#    /lib/... (glibc's 32-bit libc.so: every 32-bit link failed) name
#    /usr/lib/... (/lib64 leads to /usr/lib64 here too, so those stay)
#  - where the OS (ChromeOS's world, the read-only system under /usr and
#    /etc) has a file, the package's copy stays out and the OS's stays: the
#    image lists its files in /usr/share/nickel-gentoo/chromeos-files
#  - a command the OS has too (its names in
#    /usr/share/nickel-gentoo/chromeos-commands: python3, bash, iptables, ...)
#    goes to /usr/lib/nickel-gentoo/bin, ahead in Gentoo's PATH (users',
#    Portage's) and never in the OS's services', which keep the OS's; a link
#    to another such command (sed -> gsed) stays relative there. A
#    privileged one (setuid or setgid: fusermount3, mount, passwd, pkexec)
#    stays out: the OS's is everyone's, with ChromeOS's SELinux domain and
#    PAM setup
#  - in a package's install and removal steps (pkg_preinst, pkg_postinst,
#    pkg_prerm, pkg_postrm, pkg_config), ${EROOT}/bin, /sbin, /usr/sbin and
#    /lib mean what they mean on a merged-/usr Gentoo: the file commands
#    (ln, rm, mv, cp, chmod, chown, chgrp, touch, mkdir) take them as
#    /usr/bin and /usr/lib, or /usr/lib/nickel-gentoo/bin for a command the
#    OS has too, and a link already there is there. A few base packages
#    keep compatibility links there (tar -> gtar, gzip, bunzip2, /bin/sh)
#    and failed on the OS's read-only directories
#  - Portage itself (sys-apps/portage), two lines:
#     - it takes "read-only file system" for a directory it can't remove, as
#       it takes "not empty" or "busy": here /usr, /etc and /var are mount
#       points on a read-only root, where removing one fails that way, and
#       every unmerge stopped there (Portage tries each package's parent
#       directories up to the top), leaving the package recorded
#     - its check for read-only targets, which comes before preinst, leaves
#       out /bin, /sbin, /usr/sbin and /lib, whose files preinst moves into
#       /usr (above): bzip2, which installs into /bin, was refused
#    Upstream material, the first one at least.
__nickel_merged_usr() {
	local d t
	for d in bin sbin usr/sbin lib lib64; do
		[[ -d ${ED%/}/${d} && ! -L ${ED%/}/${d} ]] || continue
		case ${d} in
		lib) t=usr/lib ;;
		lib64) t=usr/lib64 ;;
		*) t=usr/bin ;;
		esac
		mkdir -p "${ED%/}/${t}" || die
		cp -a "${ED%/}/${d}/." "${ED%/}/${t}/" || die "NickelOS: /${d} into /${t}"
		rm -rf "${ED%/}/${d}" || die
	done
	local s
	for s in "${ED%/}"/usr/lib/*.so "${ED%/}"/usr/lib64/*.so; do
		[[ -f ${s} && ! -L ${s} ]] || continue
		head -c 256 "${s}" | grep -q 'GNU ld script' || continue
		sed -i -E 's#([( ])/lib/#\1/usr/lib/#g' "${s}" || die
	done
}

__nickel_os_files() {
	local lists=${EROOT%/}/usr/share/nickel-gentoo f rel name n=0 m=0 t
	[[ -r ${lists}/chromeos-files && -d ${ED} ]] || return 0
	local -A os=() cmd=()
	while IFS= read -r f; do os[${f}]=1; done < "${lists}/chromeos-files"
	if [[ -r ${lists}/chromeos-commands ]]; then
		while IFS= read -r f; do cmd[${f}]=1; done < "${lists}/chromeos-commands"
	fi
	local bin=${ED%/}/usr/lib/nickel-gentoo/bin
	while IFS= read -r -d '' f; do
		rel=${f#"${ED%/}"}
		name=${rel##*/}
		if [[ ${rel} == /usr/bin/* && ( -n ${os[${rel}]} || -n ${cmd[${name}]} ) ]]; then
			mkdir -p "${bin}" || die
			if [[ -L ${f} ]]; then
				t=$(readlink "${f}")
				if [[ ${t} != */* && ( -n ${os[/usr/bin/${t}]} || -n ${cmd[${t}]} ) ]]; then
					:  # both in /usr/lib/nickel-gentoo/bin: stays relative
				elif [[ ${t} != /* ]]; then
					t=/usr/bin/${t}
				fi
				ln -sfn "${t}" "${bin}/${name}" || die
				rm -f "${f}" || die
			elif [[ -u ${f} || -g ${f} ]]; then
				rm -f "${f}" || die
				n=$((n + 1))
				continue
			else
				mv -f "${f}" "${bin}/${name}" || die
			fi
			m=$((m + 1))
		elif [[ -n ${os[${rel}]} ]]; then
			rm -f "${f}" && n=$((n + 1))
		fi
	done < <(find "${ED}" \( -type f -o -type l \) -print0)
	(( n )) && einfo "NickelOS: ${n} files the OS has itself left out"
	(( m )) && einfo "NickelOS: ${m} commands the OS has too in /usr/lib/nickel-gentoo/bin"
	return 0
}

__nickel_portage() {
	[[ ${CATEGORY}/${PN} == sys-apps/portage ]] || return 0
	local f fixed=0
	for f in "${ED%/}"/usr/lib/python3*/site-packages/portage/dbapi/vartree.py; do
		[[ -f ${f} ]] || continue
		if ! grep -q 'errno.EROFS,  # NickelOS' "${f}"; then
			sed -i '/_ignored_rmdir_errnos = (/,/)/ s/^\(\s*\)errno\.EEXIST,$/&\n\1errno.EROFS,  # NickelOS: \/usr on a read-only root/' "${f}"
		fi
		if ! grep -q '# NickelOS: preinst moves' "${f}"; then
			awk '!done && /^[ \t]*rofilesystems = ro_checker\(dirs_ro\)$/ {
				match($0, /^[ \t]*/); i = substr($0, RSTART, RLENGTH)
				print i "# NickelOS: preinst moves these into /usr (the ::nickel profile)."
				print i "dirs_ro = {x for x in dirs_ro if not any(x == d or x.startswith(d + \"/\") for d in (\"/bin\", \"/sbin\", \"/usr/sbin\", \"/lib\"))}"
				done = 1
			} { print }' "${f}" > "${f}.nickel" && mv "${f}.nickel" "${f}"
		fi
		if grep -q 'errno.EROFS,  # NickelOS' "${f}" && grep -q '# NickelOS: preinst moves' "${f}"; then
			# Its compiled copies are of the file as it was.
			rm -f "${f%/*}"/__pycache__/vartree.*.pyc
			fixed=1
		fi
	done
	if (( fixed )); then
		einfo "NickelOS: Portage fitted to a read-only root (removals, /bin and /lib installs)"
	else
		ewarn "NickelOS: this Portage could not be fitted to the read-only root: removing"
		ewarn "packages may stop at /usr, and packages with files in /bin or /lib be refused"
	fi
}

__nickel_map_load() {
	[[ -n ${__nickel_map_loaded} ]] && return 0
	__nickel_map_loaded=1
	declare -gA __nickel_map_os=() __nickel_map_cmd=()
	local lists=${EROOT%/}/usr/share/nickel-gentoo f
	[[ -r ${lists}/chromeos-files ]] || return 0
	while IFS= read -r f; do [[ ${f} == /usr/bin/* ]] && __nickel_map_os[${f}]=1; done < "${lists}/chromeos-files"
	if [[ -r ${lists}/chromeos-commands ]]; then
		while IFS= read -r f; do __nickel_map_cmd[${f}]=1; done < "${lists}/chromeos-commands"
	fi
}

# The path Gentoo's world has for $1 on NickelOS.
__nickel_map() {
	local p=$1 r=${EROOT%/} d name
	# "${EROOT}/bin/x" is //bin/x with EROOT=/.
	while [[ ${p} == *//* ]]; do p=${p//\/\//\/}; done
	while [[ ${r} == *//* ]]; do r=${r//\/\//\/}; done
	case ${p} in
	"${r}"/bin/*|"${r}"/sbin/*|"${r}"/usr/sbin/*)
		d=${p%/*}
		if [[ ${d} != "${r}"/bin && ${d} != "${r}"/sbin && ${d} != "${r}"/usr/sbin ]]; then
			echo "${p}"
			return
		fi
		name=${p##*/}
		__nickel_map_load
		if [[ -n ${__nickel_map_cmd[${name}]} || -n ${__nickel_map_os[/usr/bin/${name}]} ]]; then
			echo "${r}/usr/lib/nickel-gentoo/bin/${name}"
		else
			echo "${r}/usr/bin/${name}"
		fi
		;;
	"${r}"/lib/*) echo "${r}/usr/lib/${p#"${r}"/lib/}" ;;
	*) echo "${p}" ;;
	esac
}

__nickel_mapped() {
	local cmd=$1 x a=()
	shift
	for x in "$@"; do a+=("$(__nickel_map "${x}")"); done
	command "${cmd}" "${a[@]}"
}

__nickel_ln() {
	local x a=() force= last
	for x in "$@"; do
		[[ ${x} == -*f* && ${x} != --* ]] && force=1
		a+=("$(__nickel_map "${x}")")
	done
	last=${a[-1]}
	if [[ ${last} == "${EROOT%/}"/usr/lib/nickel-gentoo/bin/* && ${#a[@]} -ge 2 ]]; then
		# A bare name that isn't in Gentoo's bin with it is in /usr/bin.
		local t=${a[-2]}
		if [[ ${t} != */* && ! -e ${last%/*}/${t} && -e ${EROOT%/}/usr/bin/${t} ]]; then
			a[-2]=/usr/bin/${t}
		fi
	fi
	local orig=${!#}
	while [[ ${orig} == *//* ]]; do orig=${orig//\/\//\/}; done
	if [[ -z ${force} && ( -e ${last} || -L ${last} ) && ${last} != "${orig}" ]]; then
		return 0
	fi
	command ln "${a[@]}"
}

# Tailscale's service (net-vpn/tailscale): ChromeOS's routing keeps each
# network's default route in a table of its own and sends sockets there by
# firewall marks; Tailscale marks its own connections (to its servers and
# peers) to go around its tunnel, and its rules then look for them in the
# main table, which has no default route here, and end in "unreachable":
# tailscaled never reached its servers ("network is unreachable"). Bound to
# the network's interface instead (Tailscale's own knob), they follow
# ChromeOS's rule for that interface.
__nickel_tailscale() {
	[[ ${CATEGORY}/${PN} == net-vpn/tailscale ]] || return 0
	local f fixed=0
	for f in "${ED%/}"/etc/conf.d/tailscale*; do
		[[ -f ${f} ]] || continue
		grep -q '^export TS_FORCE_LINUX_BIND_TO_DEVICE=' "${f}" && continue
		cat >> "${f}" <<-'CONF'

		# NickelOS: tailscaled's own connections are bound to the network's
		# interface; with Tailscale's firewall mark, ChromeOS's routing has
		# no route for them ("network is unreachable").
		export TS_FORCE_LINUX_BIND_TO_DEVICE=true
		CONF
		fixed=1
	done
	(( fixed )) && einfo "NickelOS: tailscaled binds its connections to the network's interface (/etc/conf.d)"
	return 0
}

case ${EBUILD_PHASE} in
preinst|postinst|prerm|postrm|config)
	if [[ -d ${EROOT%/}/bin && ! -L ${EROOT%/}/bin && -r ${EROOT%/}/usr/share/nickel-gentoo/chromeos-files ]]; then
		ln() { __nickel_ln "$@"; }
		rm() { __nickel_mapped rm "$@"; }
		mv() { __nickel_mapped mv "$@"; }
		cp() { __nickel_mapped cp "$@"; }
		chmod() { __nickel_mapped chmod "$@"; }
		chown() { __nickel_mapped chown "$@"; }
		chgrp() { __nickel_mapped chgrp "$@"; }
		touch() { __nickel_mapped touch "$@"; }
		mkdir() { __nickel_mapped mkdir "$@"; }
	fi
	;;
esac

if [[ ${EBUILD_PHASE} == preinst ]]; then
	__nickel_merged_usr
	__nickel_os_files
	__nickel_portage
	__nickel_tailscale
fi
