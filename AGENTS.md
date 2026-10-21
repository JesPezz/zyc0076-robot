# AGENTS — reglas de compilación y release

Reglas generales del firmware (versiones, `-Os`, IRAM, esptool): ver `esp32cam/README.md`.
Aquí solo lo operativo que NO está en el README.

## Compilar SIEMPRE en PCHOME (escritorio), no en el portátil

El portátil (WSL) tarda ~56 min y se traba; PCHOME tarda **~6 min**.

- SSH (reglas del AGENTS global):
  `ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 -i ~/.ssh/servidor_opencode jespe@192.168.1.122`
  (shell = cmd.exe; si no responde → Wake-on-LAN, ver AGENTS global)
- Árbol en PCHOME: `C:\Users\jespe\Desktop\chuyito\robot` (clon público desde GitHub)
- PlatformIO: `C:\Users\jespe\AppData\Local\Programs\Python\Python313\Scripts\pio.exe` = **6.1.19** (la que pide README)
  NO usar `C:\Python314\Scripts\pio.exe` (6.2, rompe el build)
- git está en el PATH de PCHOME ✓ (lo exige pioarduino)

Build (desde WSL, sesión larga):

```
setsid ssh -i ~/.ssh/servidor_opencode jespe@192.168.1.122 \
  "cd /d C:\Users\jespe\Desktop\chuyito\robot\esp32cam && C:\Users\jespe\AppData\Local\Programs\Python\Python313\Scripts\pio.exe run -e esp32cam" \
  > /tmp/build_pch.log 2>&1 < /dev/null & disown
```

## Sincronizar fuentes que NO están en git

`esp32cam/components/` (105MB) y `esp32cam/external/` (220MB, btstack) están
git-ignored: un `git clone` en PCHOME **no los trae** y el build falla con
`Failed to resolve component 'btstack'`. Si cambian en el portátil, copiarlos:

```
scp -i ~/.ssh/servidor_opencode -r esp32cam/components esp32cam/external \
    jespe@192.168.1.122:"C:/Users/jespe/Desktop/chuyito/robot/esp32cam/"
```

También `esp32cam/sdkconfig.esp32cam` si se tocó (garantiza misma config).

## Release OTA (reglas duras)

1. Compilar → `esp32cam/fw/firmware.bin` (copiar de vuelta desde PCHOME
   `.pio\build\esp32cam\firmware.bin` si se compiló allí).
2. Verificar `#define FW_VERSION "X.Y"` en `esp32cam/main/sketch.cpp` = tag del release.
2b. **Tamaño**: el binario NUNCA puede pasar de 1.572.864 B (ver «Tamaño máximo
   del firmware» más abajo y comprobarlo antes de publicar).
3. Publicar en WSL con gh (`gh` está en `/usr/bin/gh`, autenticado como JesPezz;
   el viejo `~/.local/bin/gh` ya no existe):

   ```
   gh release create vX.Y esp32cam/fw/firmware.bin <uno.hex> <uno.txt> \
     -R JesPezz/zyc0076-robot --title "vX.Y — <resumen>" --notes "<cambios>"
   ```

   `uno.txt` es **una línea con la marca de versión del UNO** (p. ej. `V3`) y tiene
   que coincidir con el `F(" V3")` de `reportSensors()` en
   `arduino/Arduino-UNO/Arduino-UNO.ino`; `uno.hex` es ese sketch compilado con
   arduino-cli. El ESP32 compara la marca con la que el UNO reporta por serie y solo
   entonces baja `uno.hex` y lo programa (idempotente: si coinciden, no toca nada).

4. **NUNCA `--prerelease`**: el OTA consulta `releases/latest` y excluye prereleases.
5. Los assets se llaman **exactamente** `firmware.bin` (ESP32), `uno.hex` (sketch del
   UNO) y `uno.txt` (su marca de versión): el OTA los busca por nombre.
   Si salió prerelease: `gh release edit vX.Y --prerelease=false`.

## Tamaño máximo del firmware (REGLA DURA)

El binario NUNCA puede pasar de **1.572.864 B** = 1,5 MB = slot `ota_0` (`0x180000`).

- **Por qué**: si se pasa, la OTA falla SIEMPRE (`Update` no cabe en la partición) y
  el flasheo por cable escribe 6,5 KB de más sobre la cabecera de `ota_1` → el
  bootloader no puede arrancar ninguna de las dos particiones y el robot queda en
  bucle, sin WiFi y sin forma de recuperarlo por aire. Pasó con **v1.4 = 1.579.488 B**.
- **Comprobar SIEMPRE antes de publicar** (sale con exit≠0 si se pasa):

  ```
  python3 -c "import os,sys;s=os.path.getsize('esp32cam/fw/firmware.bin');sys.exit(f'FUERA DE SLOT: {s} B' if s>1572864 else 0)" && echo CABE
  ```

- **Si se pasa, recortar** (esto ya está aplicado en `sdkconfig.defaults`, no quitarlo):
  `CONFIG_LOG_DEFAULT_LEVEL_NONE` + `CONFIG_LOG_MAXIMUM_LEVEL=0` +
  `CONFIG_BOOTLOADER_LOG_LEVEL_NONE` (≈ -82 KB), `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS=3`,
  `CONFIG_MBEDTLS_ERROR_STRINGS=n` y las curvas ECP sobrantes a `n`, dejando
  **`SECP256R1`, `SECP384R1` y `CURVE25519`**. GitHub firma con P-384: sin ella el
  TLS muere con `MBEDTLS_ERR_PK_UNKNOWN_NAMED_CURVE` (-0x3A00) y en el log solo se ve
  `handshake=0`, así que la consulta del release falla en cada arranque (pasó hasta la
  v1.25; la regla vieja, «solo SECP256R1 y CURVE25519», era FALSA).
  Cuidado: `sdkconfig.esp32cam` ya generado NO se re-genera solo — hay que
  parchearlo también o el recorte no entra en el build.
- **Referencia**: v1.5 = 1.488.688 B (84 KB de margen). v1.3 = 1.497.712 B.

## App Android (APK) — se actualiza a sí misma

- Compilar en PCHOME: `cd /d C:\Users\jespe\Desktop\chuyito\robot\app && build_apk.bat`
  (aapt2 + javac + d8 + zipalign + apksigner, sin Gradle) → `app/robot.apk`.
- **La clave de firma NO se versiona** (el repo es público). Vive en PCHOME
  (`app/key.jks`) y en Nextcloud › Escritorio › `zyc0076-keys/app-key.jks`
  (alias `k`, storepass/keypass `android`, dname `CN=zyc0076`). Copiarla a
  `app/key.jks` antes de compilar: `build_apk.bat` solo la genera si NO existe,
  y un APK con otra clave no se puede instalar encima (habría que desinstalar).
- Publicar versión nueva: subir `versionCode` y `versionName` en
  `app/AndroidManifest.xml`, compilar, copiar `robot.apk` al repo y commitear
  (el APK SÍ se versiona). La app se actualiza sola bajando
  `raw.githubusercontent.com/JesPezz/zyc0076-robot/main/app/robot.apk` y
  comparando el `versionName` del APK bajado con el instalado.
- La clave antigua (la que se commiteó por error) está **quemada**: no usarla.

## Grabar el ESP32 por USB (lo fiable)

```
python3 -m esptool --chip esp32 -p /dev/ttyUSB0 -b 460800 write_flash 0x10000 <bin>
```

~24 s, hash verificado. **Quitar antes el pack del BAT**: el UNO habla por la misma
UART0 y esptool falla con `Invalid head of packet (0xA5): Possible serial noise or
corruption`. Si al terminar el chip se queda en modo descarga (0 bytes por serie),
se arranca con un pulso RTS (`rts=True; sleep(0.3); rts=False`) o el botón RESET.
La OTA por WiFi (`POST /update`) solo sirve de emergencia: a −60 dBm muere a mitad.

## Último recurso: compilar en el portátil

Solo si PCHOME no está disponible (WOL falla). Desde `workdir esp32cam/`:

```
setsid cmd.exe /c "set PATH=C:\Users\jespe\PortableGit\bin;%PATH% && C:\Users\jespe\.platformio\penv\Scripts\pio.exe run -e esp32cam" > /tmp/build_v14.log 2>&1 < /dev/null & disown
```

(git NO está en el PATH de Windows; pio es 6.1.18 de `.platformio\penv` — sin
`setsid + disown` la herramienta corta el proceso al agotar timeout).
