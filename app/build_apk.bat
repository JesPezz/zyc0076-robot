@echo off
set JAVA_HOME=C:\devtools\jdk\jdk-17.0.20.1+1
set PATH=%JAVA_HOME%\bin;%PATH%
set SDK=C:\Android
set BT=%SDK%\build-tools\34.0.0
set PLAT=%SDK%\platforms\android-34\android.jar
cd /d "%~dp0"
if exist build rmdir /s /q build
mkdir build

echo [1/6] aapt2 link
"%BT%\aapt2.exe" link -o build\base.apk -I "%PLAT%" --manifest AndroidManifest.xml --min-sdk-version 24 --target-sdk-version 34 || goto err

echo [2/6] javac
javac -source 1.8 -target 1.8 -bootclasspath "%PLAT%" -d build\classes src\com\jespezz\zyc0076\*.java || goto err

echo [3/6] d8
call "%BT%\d8.bat" --lib "%PLAT%" --min-api 24 --output build build\classes\com\jespezz\zyc0076\*.class || goto err

echo [4/6] meter classes.dex en el apk
copy /y build\base.apk build\app.apk >nul
"C:\Users\jespe\AppData\Local\Programs\Python\Python313\python.exe" -c "import zipfile;z=zipfile.ZipFile(r'build\app.apk','a');z.write(r'build\classes.dex','classes.dex');z.close()" || goto err

echo [5/6] zipalign
"%BT%\zipalign.exe" -f 4 build\app.apk build\aligned.apk || goto err

echo [6/6] firmar
if not exist key.jks keytool -genkeypair -keystore key.jks -alias k -keyalg RSA -keysize 2048 -validity 10000 -storepass android -keypass android -dname "CN=zyc0076" || goto err
call "%BT%\apksigner.bat" sign --ks key.jks --ks-pass pass:android --key-pass pass:android --ks-key-alias k --out robot.apk build\aligned.apk || goto err

echo DONE_APK
dir /b robot.apk
goto :eof

:err
echo ERROR_APK
