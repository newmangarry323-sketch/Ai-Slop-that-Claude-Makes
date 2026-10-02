# Read the usual settings first.
[ -f ~/.profile ] && . ~/.profile

# On the first console, start the graphical desktop (SkarletOS).  If it
# stops, start it again; after three quick failures, stay on the console.
if [ -z "${DISPLAY:-}" ] && [ "$(tty)" = /dev/tty1 ]; then
    mkdir -p ~/.local/share
    tries=0
    while [ $tries -lt 3 ]; do
        started=$(date +%s)
        startx -- -nolisten tcp vt1 > ~/.local/share/xorg-session.log 2>&1 || true
        [ $(( $(date +%s) - started )) -lt 20 ] && tries=$((tries + 1)) || tries=0
    done
    echo "The desktop did not start; see ~/.local/share/xorg-session.log and /var/log/Xorg.0.log"
fi
