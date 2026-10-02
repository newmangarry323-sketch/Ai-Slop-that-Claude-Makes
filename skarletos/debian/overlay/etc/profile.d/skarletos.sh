# A tip in new Skarlet Terminal windows.
if [ -n "${SKARLET_TERMINAL:-}" ] && [ -z "${SKARLET_TIP_SHOWN:-}" ]; then
    export SKARLET_TIP_SHOWN=1
    printf '\033[1mWelcome to SkarletOS\033[0m (Debian %s)\n' "$(cat /etc/debian_version 2>/dev/null)"
    printf 'Install software:  sudo apt update && sudo apt install NAME\n'
    printf 'Find software:     apt search WORD      Remove:  sudo apt remove NAME\n'
    if [ -d /run/live ]; then
        printf 'This is the live system: changes are lost at shutdown.\n'
        printf 'To keep them, install to the disk:  sudo skarlet-install\n'
    fi
    echo
fi
