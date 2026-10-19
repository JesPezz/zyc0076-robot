package com.jespezz.zyc0076;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.text.InputType;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.EditText;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

public class MainActivity extends Activity {
    // El APK va versionado en el repo: raw.githubusercontent no toca el canal de
    // releases del firmware, que compara el tag de releases/latest.
    private static final String APK_URL =
            "https://raw.githubusercontent.com/JesPezz/zyc0076-robot/main/app/robot.apk";
    private static final String AUTORIDAD = "com.jespezz.zyc0076.apk";
    private static final String APK_LOCAL = "robot.apk";

    private WebView wv;
    private SharedPreferences prefs;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        prefs = getSharedPreferences("p", 0);

        wv = new WebView(this);
        WebSettings s = wv.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setLoadWithOverviewMode(true);
        s.setUseWideViewPort(true);
        wv.setWebViewClient(new WebViewClient() {
            @Override
            public void onReceivedError(WebView v, WebResourceRequest r, WebResourceError e) {
                if (r != null && r.isForMainFrame()) askIp();
            }
        });

        setContentView(wv);

        String ip = prefs.getString("ip", null);
        if (ip == null) askIp();
        else wv.loadUrl("http://" + ip + "/");

        buscaActualizacion();
    }

    // ------------------------------------------- OTA del propio APK (se firma con
    // la misma clave que el instalado; Android rechaza cualquier otra)
    private void buscaActualizacion() {
        new Thread(new Runnable() { public void run() {
            final File f = new File(getCacheDir(), APK_LOCAL);
            try {
                HttpURLConnection c = (HttpURLConnection) new URL(APK_URL).openConnection();
                c.setConnectTimeout(6000);
                c.setReadTimeout(60000);
                InputStream in = c.getInputStream();
                FileOutputStream out = new FileOutputStream(f);
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                out.close();
                in.close();
                c.disconnect();
            } catch (Exception e) {
                f.delete();     // sin red: no molestar
                return;
            }

            PackageInfo nueva = getPackageManager().getPackageArchiveInfo(f.getAbsolutePath(), 0);
            String actual = null;
            try {
                actual = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
            } catch (Exception e) { }
            if (nueva == null || actual == null || !masNueva(nueva.versionName, actual)) {
                f.delete();     // igual o mas vieja: nada que hacer
                return;
            }
            final String v = nueva.versionName;
            runOnUiThread(new Runnable() { public void run() { instala(f, v); }});
        }}).start();
    }

    // "1.10" es mas nueva que "1.9"
    private static boolean masNueva(String a, String b) {
        String[] x = a.split("\\."), y = b.split("\\.");
        for (int i = 0; i < Math.max(x.length, y.length); i++) {
            int n = i < x.length ? Integer.parseInt(x[i].trim()) : 0;
            int m = i < y.length ? Integer.parseInt(y[i].trim()) : 0;
            if (n != m) return n > m;
        }
        return false;
    }

    private void instala(final File apk, final String v) {
        // Desde Android 8 hace falta autorizar "instalar apps desconocidas"
        if (Build.VERSION.SDK_INT >= 26 && !getPackageManager().canRequestPackageInstalls()) {
            new AlertDialog.Builder(this)
                .setTitle("Actualizacion v" + v)
                .setMessage("Autoriza a ZYC0076 a instalar apps y vuelve a abrirla.")
                .setPositiveButton("Ajustes", new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface d, int w) {
                        startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                                Uri.parse("package:" + getPackageName())));
                    }
                })
                .setNegativeButton("Luego", null)
                .show();
            return;
        }
        Uri u = Uri.parse("content://" + AUTORIDAD + "/" + apk.getName());
        Intent i = new Intent(Intent.ACTION_VIEW);
        i.setDataAndType(u, "application/vnd.android.package-archive");
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            startActivity(i);
        } catch (Exception e) {
            Toast.makeText(this, "No hay instalador disponible", Toast.LENGTH_LONG).show();
        }
    }

    private void askIp() {
        final EditText e = new EditText(this);
        e.setInputType(InputType.TYPE_CLASS_TEXT);
        e.setText(prefs.getString("ip", "192.168.1.211"));
        new AlertDialog.Builder(this)
            .setTitle("IP del robot")
            .setMessage("IP del ESP32-CAM")
            .setView(e)
            .setCancelable(false)
            .setPositiveButton("Conectar", new DialogInterface.OnClickListener() {
                @Override
                public void onClick(DialogInterface d, int w) {
                    String ip = e.getText().toString().trim();
                    prefs.edit().putString("ip", ip).apply();
                    wv.loadUrl("http://" + ip + "/");
                }
            })
            .show();
    }

    @Override
    public void onBackPressed() {
        askIp();
    }
}
