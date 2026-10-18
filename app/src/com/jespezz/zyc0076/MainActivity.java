package com.jespezz.zyc0076;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.text.InputType;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.EditText;

public class MainActivity extends Activity {
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
