# cfl shell integration — cd to last browsed directory after Esc quit
#
# Install:  sudo cp cfl.sh /usr/share/cfl/cfl.sh
# Enable:   echo 'source /usr/share/cfl/cfl.sh' >> ~/.bashrc   # or ~/.zshrc
#
# After this, running `cfl` and quitting with Esc leaves the shell in the last
# directory you navigated to. Ctrl+Q quits without writing lastdir, so CWD stays.
# The bare binary cannot chdir its parent — this wrapper does.

cfl() {
	# Avoid nested function recursion if something re-sources oddly
	local lastdir="${CFL_LASTDIR:-${XDG_CONFIG_HOME:-$HOME/.config}/cfl/lastdir}"
	export CFL_LASTDIR="$lastdir"

	command cfl "$@"
	local ret=$?

	if [ -f "$CFL_LASTDIR" ]; then
		local d
		# Read first line only (path may contain spaces)
		IFS= read -r d < "$CFL_LASTDIR" || d=""
		rm -f -- "$CFL_LASTDIR"
		if [ -n "$d" ] && [ -d "$d" ]; then
			cd -- "$d" || true
		fi
	fi

	return "$ret"
}
