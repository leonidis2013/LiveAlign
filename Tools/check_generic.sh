#!/usr/bin/env bash
# LiveAlign stays generic: any speaker, any amplifier, any processor, any analyser. No maker's or product's name may
# appear in the source, the tests, the test data or the default speaker library (user files may hold any names).
# Fails with the lines found. Add a word to the list when a new brand slips in.
set -u
cd "$(dirname "$0")/.."
words='jbl|yamaha|cbt ?70|dxs ?12|smaart|systune|driverack|\bdbx\b|\bcrown (audio|amp|xli)\b|shure|behringer|sonarworks|focusrite|\bqsc\b|meyer|l-acoustics|\bbose\b|electro-voice|\bk-array\b|\bnexo\b|\bmartin audio\b|\bdanley\b|\brcf\b|\bmackie\b|\bpresonus\b|\bbiamp\b|\bsymetrix\b|\bxilica\b|\blake\b|\bgalaxy\b|\bq-sys\b|\bbss\b|\bsoundweb\b|\bxta\b|\ballen ?& ?heath\b|\bsq-?5\b|\bmidas\b|\bavantis\b|\bdigico\b|\bearthworks\b|\bisemcon\b'
found=$(grep -rniE "$words" Source Tests Tools README.md CMakeLists.txt --include='*.h' --include='*.cpp' --include='*.txt' --include='*.md' --include='*.sh' --include='*.py' 2>/dev/null \
        | grep -v 'Tools/check_generic.sh' || true)
if [ -n "$found" ]; then
  echo "Brand or model names found (LiveAlign stays generic):"
  echo "$found"
  exit 1
fi
echo "Generic check: no brand or model names."
