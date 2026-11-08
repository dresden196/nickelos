# Copyright 2026 The NickelOS Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=7

# NickelOS updates (docs/vision.md "Updates"): what update_engine needs to
# take updates from NickelOS's server.
#
# The public key update payloads are verified with. update_engine prefers
# this file to any key the update server sends, so a payload must be signed
# with its private half (nickel-update-publish). On a release image
# (official to update_engine: no cros_debug on the kernel command line) the
# server's key is never used and without this file nothing could be
# verified. It is NickelOS's PRODUCTION key (made 2026-10-12, SHA-256 of its
# DER 3e4df953f2e947abb86f02d3dd005385301d6a0d71db696b8a61f86fdcf70420); its
# private half is only on Dresden's removable media and laptop, and
# nickel-update-sign signs with it there (docs/updates.md, "Keys").
# update-payload-key-test.pub.pem is the TEST key, whose private half is on
# nickel-next: nickel-build-stock puts it into test images, and into base
# images built for tests, so the VM tests can sign their own updates
# (nickel-image-testkey). Release images keep the production key.
#
# Let's Encrypt's roots (ISRG Root X1, RSA, and X2, ECDSA), in the small
# set of authorities update_engine trusts on release images
# (/usr/share/chromeos-ca-certificates, chromeos-ca-certificates, which
# has neither): update.nickelos.dev's certificate is Let's Encrypt's. The
# links are OpenSSL's subject-hash names (openssl x509 -subject_hash),
# which curl looks the issuer up by; nothing else there has these hashes.
#
# nickel-oobe-done: marks setup as done when a user session starts, which
# update_engine waits for before checking by itself (Chrome's own marker
# came only at the second sign-in on NickelOS).

DESCRIPTION="NickelOS: what update_engine needs (payload key, CA roots, setup-done marker)"
HOMEPAGE="https://nickelos.dev"

LICENSE="BSD-Google"
SLOT="0"
KEYWORDS="*"

S="${WORKDIR}"

src_install() {
	insinto /usr/share/update_engine
	doins "${FILESDIR}"/update-payload-key.pub.pem

	local ca=/usr/share/chromeos-ca-certificates
	insinto "${ca}"
	doins "${FILESDIR}"/ISRG_Root_X1.pem "${FILESDIR}"/ISRG_Root_X2.pem
	dosym ISRG_Root_X1.pem "${ca}"/4042bcee.0
	dosym ISRG_Root_X2.pem "${ca}"/0b9bc432.0

	insinto /etc/init
	doins "${FILESDIR}"/init/nickel-oobe-done.conf
}
