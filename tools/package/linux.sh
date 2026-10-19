#!/usr/bin/env bash
# Empaqueta lo compilado en Linux: tar.gz (con los tres ejecutables), .deb y la carpeta AppDir (de ahí sale el AppImage).
# Uso: tools/package/linux.sh <carpeta de compilación> <versión> <carpeta de salida>
#   p. ej.: tools/package/linux.sh build/linux-release 1.0.0 dist
set -euo pipefail

build=${1:?falta la carpeta de compilación (p. ej. build/linux-release)}
version=${2:?falta la versión (p. ej. 1.0.0)}
out=${3:?falta la carpeta de salida}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

bins=("$build/apps/mcweb/mcweb" "$build/apps/mcweb-server/mcweb-server" "$build/apps/mcweb-wsproxy/mcweb-wsproxy")
for b in "${bins[@]}"; do
  [ -x "$b" ] || { echo "no está compilado: $b" >&2; exit 1; }
done
mkdir -p "$out"
out=$(cd "$out" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

desktop() {
  cat <<DESK
[Desktop Entry]
Type=Application
Name=MC-WEB
Comment=Reimplementación libre de Minecraft 1.8 para escritorio y navegador
Exec=mcweb
Icon=mcweb
Categories=Game;
Terminal=false
DESK
}

# --- tar.gz: los ejecutables, la licencia y el README ---
name="mcweb-$version-linux-x86_64"
mkdir -p "$work/$name"
for b in "${bins[@]}"; do install -m 755 "$b" "$work/$name/"; done
cp "$root/LICENSE" "$root/README.md" "$work/$name/"
cp -r "$root/docs" "$work/$name/docs"
tar -C "$work" -czf "$out/$name.tar.gz" "$name"

# --- carpeta de archivos de instalación (la usan el .deb y el AppDir) ---
tree="$work/tree"
mkdir -p "$tree/usr/bin" "$tree/usr/share/applications" "$tree/usr/share/icons/hicolor/256x256/apps" "$tree/usr/share/doc/mcweb"
for b in "${bins[@]}"; do install -m 755 "$b" "$tree/usr/bin/"; done
desktop > "$tree/usr/share/applications/mcweb.desktop"
install -m 644 "$here/mcweb.png" "$tree/usr/share/icons/hicolor/256x256/apps/mcweb.png"
install -m 644 "$root/LICENSE" "$tree/usr/share/doc/mcweb/copyright"

# --- .deb ---
size_kb=$(du -sk "$tree" | cut -f1)
control="Package: mcweb
Version: $version
Section: games
Priority: optional
Architecture: amd64
Installed-Size: $size_kb
Depends: libc6, libstdc++6
Recommends: libgl1, libx11-6, libasound2t64 | libasound2 | libpulse0
Maintainer: MC-WEB <noreply@users.noreply.github.com>
Homepage: https://github.com/macorredor270/MC-WEB
Description: reimplementación libre de Minecraft 1.8
 MC-WEB es una reimplementación en sala limpia de Minecraft 1.8 en C++20 y SDL3, con
 servidor dedicado propio (mcweb-server) y un puente WebSocket para el navegador (mcweb-wsproxy).
 No incluye nada de Mojang: usa un pack libre o los recursos que descarga quien juega."
deb="$out/mcweb_${version}_amd64.deb"
if command -v dpkg-deb >/dev/null 2>&1; then
  mkdir -p "$tree/DEBIAN"
  printf '%s\n' "$control" > "$tree/DEBIAN/control"
  dpkg-deb --root-owner-group --build "$tree" "$deb" >/dev/null
  rm -rf "$tree/DEBIAN"
else  # sin dpkg-deb (otras distribuciones): un .deb es un archivo `ar` con tres miembros
  mkdir -p "$work/deb"
  printf '2.0\n' > "$work/deb/debian-binary"
  mkdir -p "$work/ctl"
  printf '%s\n' "$control" > "$work/ctl/control"
  tar --owner=0 --group=0 -C "$work/ctl" -czf "$work/deb/control.tar.gz" .
  tar --owner=0 --group=0 -C "$tree" -czf "$work/deb/data.tar.gz" .
  rm -f "$deb"
  (cd "$work/deb" && ar rc "$deb" debian-binary control.tar.gz data.tar.gz)
fi

# --- AppDir (appimagetool lo convierte en AppImage) ---
appdir="$out/AppDir"
rm -rf "$appdir"
cp -r "$tree" "$appdir"
cp "$appdir/usr/share/applications/mcweb.desktop" "$appdir/mcweb.desktop"
cp "$here/mcweb.png" "$appdir/mcweb.png"
cat > "$appdir/AppRun" <<'RUN'
#!/bin/sh
here=$(dirname "$(readlink -f "$0")")
exec "$here/usr/bin/mcweb" "$@"
RUN
chmod +x "$appdir/AppRun"

ls -la "$out"
