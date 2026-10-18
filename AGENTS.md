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
3. Publicar en WSL con gh (`~/.local/bin/gh`, autenticado como JesPezz):

   ```
   ~/.local/bin/gh release create vX.Y esp32cam/fw/firmware.bin -R JesPezz/zyc0076-robot \
     --title "vX.Y — <resumen>" --notes "<cambios>"
   ```

4. **NUNCA `--prerelease`**: el OTA consulta `releases/latest` y excluye prereleases.
5. El asset debe llamarse **exactamente** `firmware.bin` (el OTA lo busca por nombre).
   Si salió prerelease: `gh release edit vX.Y --prerelease=false`.

## Último recurso: compilar en el portátil

Solo si PCHOME no está disponible (WOL falla). Desde `workdir esp32cam/`:

```
setsid cmd.exe /c "set PATH=C:\Users\jespe\PortableGit\bin;%PATH% && C:\Users\jespe\.platformio\penv\Scripts\pio.exe run -e esp32cam" > /tmp/build_v14.log 2>&1 < /dev/null & disown
```

(git NO está en el PATH de Windows; pio es 6.1.18 de `.platformio\penv` — sin
`setsid + disown` la herramienta corta el proceso al agotar timeout).
