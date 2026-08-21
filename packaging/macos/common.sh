# Shared helpers for the macOS packaging scripts. Sourced by
# build-package.sh and sign-package.sh after they define the globals used
# here: application, artifact_dir, dmg, dmg_extras, dmg_volume_name,
# sign_identity, notary_profile, notary_apple_id, notary_team_id,
# notary_password, work_root, and optionally dsym_bundle (a debug-symbol
# bundle packaged beside the application in the DMG).

notary_arguments=()
notarize_enabled=0
dsym_bundle="${dsym_bundle:-}"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1" >&2
        exit 1
    fi
}

# Notarization uses either a stored keychain profile or direct credentials
# passed through the environment (for non-interactive use). Resolved before
# any build work so a misconfiguration fails fast.
resolve_notary_arguments() {
    if [[ -n "${notary_profile}" ]]; then
        notary_arguments=(--keychain-profile "${notary_profile}")
        notarize_enabled=1
    elif [[ -n "${notary_apple_id}${notary_team_id}${notary_password}" ]]; then
        if [[ -z "${notary_apple_id}" || -z "${notary_team_id}" ||
                -z "${notary_password}" ]]; then
            echo "TIE_MACOS_NOTARY_APPLE_ID, TIE_MACOS_NOTARY_TEAM_ID, and" >&2
            echo "TIE_MACOS_NOTARY_PASSWORD must be provided together" >&2
            exit 1
        fi
        notary_arguments=(--apple-id "${notary_apple_id}"
            --team-id "${notary_team_id}" --password "${notary_password}")
        notarize_enabled=1
    fi

    if [[ "${notarize_enabled}" -eq 1 && "${sign_identity}" == "-" ]]; then
        echo "Notarization requires TIE_MACOS_SIGN_IDENTITY" >&2
        exit 1
    fi
}

sign_bundle() {
    local sign_arguments=(--force --sign "${sign_identity}")
    local item

    if [[ "${sign_identity}" != "-" ]]; then
        sign_arguments+=(--options runtime --timestamp)
    fi

    while IFS= read -r item; do
        if file "${item}" | grep -q "Mach-O"; then
            codesign "${sign_arguments[@]}" "${item}"
        fi
    done < <(find "${application}/Contents/Frameworks" -type f -print)

    codesign "${sign_arguments[@]}" "${application}"
}

create_dmg() {
    local staging="${work_root}/dmg-root"
    local attempt

    rm -rf "${staging}" "${dmg}"
    mkdir -p "${staging}" "${artifact_dir}"
    ditto "${application}" "${staging}/OpenTIE.app"
    ln -s /Applications "${staging}/Applications"

    if [[ -n "${dsym_bundle}" ]]; then
        ditto "${dsym_bundle}" "${staging}/OpenTIE.dSYM"
    fi

    # Optional pre-baked Finder layout, authored once from a styled volume
    # named "${dmg_volume_name}"; see packaging/macos/README.md.
    if [[ -f "${dmg_extras}/DS_Store" ]]; then
        cp "${dmg_extras}/DS_Store" "${staging}/.DS_Store"
    fi
    if [[ -d "${dmg_extras}/background" ]]; then
        ditto "${dmg_extras}/background" "${staging}/.background"
    fi

    # hdiutil create intermittently fails with "resource busy" on hosts where
    # Spotlight or quarantine services touch the temporary volume.
    for attempt in 1 2 3; do
        if hdiutil create \
                -volname "${dmg_volume_name}" \
                -srcfolder "${staging}" \
                -fs APFS \
                -format ULMO \
                -ov "${dmg}"; then
            break
        fi
        if [[ "${attempt}" -eq 3 ]]; then
            echo "hdiutil create failed after ${attempt} attempts" >&2
            exit 1
        fi
        sleep 5
    done

    if [[ "${sign_identity}" != "-" ]]; then
        codesign --force --sign "${sign_identity}" --timestamp "${dmg}"
    fi
}

# Notarize and staple the application first so a copy dragged out of the disk
# image retains its ticket offline. The DMG built from the stapled application is then
# submitted and stapled itself; its contents are already known to Apple, so
# the second submission is quick.
notarize_application() {
    local notary_zip="${work_root}/notary/OpenTIE.zip"

    mkdir -p "${work_root}/notary"
    rm -f "${notary_zip}"
    ditto -c -k --sequesterRsrc --keepParent "${application}" "${notary_zip}"
    xcrun notarytool submit "${notary_zip}" "${notary_arguments[@]}" --wait
    xcrun stapler staple "${application}"
}

notarize_dmg() {
    xcrun notarytool submit "${dmg}" "${notary_arguments[@]}" --wait
    xcrun stapler staple "${dmg}"
}
