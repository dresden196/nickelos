# Sourced by the UI tests that make their first account through setup
# (cloud-ui, desktop-ui, other-drives-ui, polish-ui, language, experience),
# after their vm, wait_log, wait_state and state functions; uses $V.
# local-account-test.sh sources it for quiet_choice_screen.
#
# Setup's account form is typed into after fixed waits. When a field missed
# its typing the session never started and the test went on blind, with no
# picture of why (cloud-ui on builds 23e and 23f, polish-ui and
# other-drives-ui on 22). Now a try that starts no session leaves a
# screenshot ($V/flow/<name>-setup-<try>.png) and setup's last screens in the
# log, and the form is cleared and filled again: three tries. The output of
# a first try that works is what it always was.
#   setup_account NAME PASSWORD SHOTNAME
setup_account() {
	local name=$1 pass=$2 tag=$3 try
	wait_log "AdvanceToScreen" 60
	quiet_choice_screen
	sleep 15
	# "Get started": a click can come before the button takes it (17.4's
	# release check); again until setup's next screen.
	local _
	for _ in 1 2 3; do
		nickel-vmctl click 214 649
		wait_log "SetCurrentScreen: nickel-setup" 15 >/dev/null && break
	done
	sleep 5
	# Setup's choice (Chrome 0089): Minimal unless NICKEL_TEST_EXPERIENCE=full,
	# so test VMs don't each download the Full experience's apps (~2 GB). Focus
	# starts on Next: back to the cards, down to Minimal, on to Next.
	wait_log "SetCurrentScreen: nickel-setup" 30 && { sleep 5
	  if [ "${NICKEL_TEST_EXPERIENCE:-minimal}" = minimal ]; then
	    nickel-vmctl key shift-tab; sleep 1; nickel-vmctl key down; sleep 1
	    nickel-vmctl key tab; sleep 1
	  fi
	  nickel-vmctl key ret; sleep 5; }
	for try in 1 2 3; do
		wait_log "SetCurrentScreen: local-account-creation" 30
		setup_focus_name
		setup_field "$name" "$try"
		setup_name_to_password
		setup_field "$pass" "$try"
		nickel-vmctl key tab; sleep 0.5
		setup_field "$pass" "$try"
		sleep 8
		nickel-vmctl click 1077 647
		if [ "$try" = 3 ]; then
			wait_state started 40 && { echo "session started"; return 0; }
		else
			wait_state started 40 >/dev/null && { echo "session started"; return 0; }
		fi
		nickel-vmctl shot "${V:-/srv/nickel/vmtest}/flow/$tag-setup-$try.png" >/dev/null 2>&1
		echo "setup, try $try: no session (now $(state)); ${V:-/srv/nickel/vmtest}/flow/$tag-setup-$try.png"
		vm "grep -E 'AdvanceToScreen|SetCurrentScreen|exited with|No screen' /var/log/chrome/chrome | sed 's/.*\] //' | cut -c1-90 | tail -4" | sed 's/^/  /'
		# Why an account wasn't made (signin-fatal-error, 17.4's release
		# check): the account service's and cryptohome's last words.
		vm "grep -hE 'nickel-accounts|cryptohomed|ERROR.*(login|auth)' /var/log/messages | tail -6 | cut -c1-200" | sed 's/^/  /'
	done
	return 1
}

# On an image whose setup also names the computer with the first account
# (Chrome 0102, with nickel-hostname r1 letting setup do it), the computer's
# name comes right after the account's name, and the form starts higher:
# setup_focus_name clicks into the name field (keyboard focus is in the page
# only after a click into it; 850,250 is the computer's name there), and
# setup_name_to_password goes on past the username (and the computer's
# name) to the password. update-test.sh has its own copy (it doesn't source
# this file).
setup_names_computer() {
	vm 'grep -q in_setup /usr/libexec/nickel-hostname/nickel-hostnamed 2>/dev/null'
}
setup_focus_name() {
	if setup_names_computer; then
		nickel-vmctl click 850 177
	else
		nickel-vmctl click 850 250
	fi
	sleep 1
}
setup_name_to_password() {
	if setup_names_computer; then
		nickel-vmctl key tab; sleep 0.5
	fi
	nickel-vmctl key tab; sleep 0.5; nickel-vmctl key tab; sleep 0.5
}

# A Minimal device's first browser window asks which search engine to use
# (Chrome 0092: Chrome's choice screen, a dialog over the window). The tests
# that type into the browser need it out of the way, so test VMs get
# Chrome's switch that turns it off, from the welcome screen on (one restart
# of the UI, before setup). experience-test.sh minimal checks the choice
# screen itself: NICKEL_TEST_CHOICE=1 keeps it.
# A test that restarts the UI before setup itself (for DevTools) puts the
# switch in then (language-test.sh: a second restart would lose the keyboard
# chosen on the welcome screen); then there's nothing to do here.
quiet_choice_screen() {
	[ "${NICKEL_TEST_CHOICE:-0}" = 1 ] && return 0
	vm 'grep -q disable-search-engine-choice-screen /etc/chrome_dev.conf' && return 0
	vm 'mount -o remount,rw / 2>/dev/null
echo --disable-search-engine-choice-screen >> /etc/chrome_dev.conf
stop ui >/dev/null; rm -f /var/log/chrome/chrome; start ui >/dev/null'
	wait_log "AdvanceToScreen" 60
}

# Signing in again after the UI restarts (for DevTools). The password field
# takes the typing only once the sign-in screen is up: other-drives-ui on
# 23t lost its first sign-in twice that way, and every later step ran
# without a session. A try that starts no session leaves a screenshot
# ($V/flow/sign-in-<try>.png) and is made again, three times. Then the
# session's own prompts are put away (Welcome Recap: Esc; its introduction:
# "Got it").
#   sign_in_again PASSWORD
sign_in_again() {
	local pass=$1 try
	for try in 1 2 3; do
		sleep 30
		nickel-vmctl click 640 445; sleep 3
		if [ "$try" != 1 ]; then
			nickel-vmctl key ctrl-a; sleep 0.3
			nickel-vmctl key backspace; sleep 0.3
		fi
		nickel-vmctl type "$pass"; nickel-vmctl key ret
		wait_state started 40 >/dev/null && break
		nickel-vmctl shot "$V/flow/sign-in-$try.png" >/dev/null 2>&1
		echo "sign-in, try $try: no session (now $(state))"
	done
	sleep 30
	for _ in $(seq 12); do nickel-vmctl key esc; sleep 5; done
	nickel-vmctl click 638 536; sleep 3
}

# One field: on a second try it still holds the first try's text.
setup_field() {
	if [ "$2" != 1 ]; then
		nickel-vmctl key ctrl-a; sleep 0.3
		nickel-vmctl key backspace; sleep 0.3
	fi
	nickel-vmctl type "$1"; sleep 1
}

# ui restarted, as these tests restart it to give Chrome its DevTools port.
# Upstart sometimes answers "Job failed to restart" (polish-ui on build 23g,
# other-drives-ui on 23k; neither again when rerun): Chrome then never came
# back, and every later step failed blind on DevTools. Now what the job and
# init said goes to the log, the UI is started again and the test goes on;
# its output then differs from the accepted run's, so the check shows it.
restart_ui() {
	local out
	out=$(vm 'restart ui 2>&1 >/dev/null')
	[ -z "$out" ] && return 0
	echo "restart ui: $out"
	vm 'status ui; grep -aE "init: ui|ui-pre-start|ui-post-stop" /var/log/messages | tail -6
tail -3 /var/log/ui/ui.LATEST' | cut -c1-200 | sed 's/^/  /'
	vm 'start ui >/dev/null 2>&1; status ui' | sed 's/^/  then: /'
}
