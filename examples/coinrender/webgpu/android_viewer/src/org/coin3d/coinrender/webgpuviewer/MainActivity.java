package org.coin3d.coinrender.webgpuviewer;

import android.app.Activity;
import android.content.pm.ApplicationInfo;
import android.graphics.Bitmap;
import android.net.Uri;
import android.net.http.SslError;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.webkit.ConsoleMessage;
import android.webkit.ServiceWorkerClient;
import android.webkit.ServiceWorkerController;
import android.webkit.SslErrorHandler;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;

/** Diagnostic viewer. Android's per-app TLS verifier remains authoritative. */
public final class MainActivity extends Activity {
    private static final String TAG = "CoinRenderViewer";
    private final Handler handler = new Handler(Looper.getMainLooper());
    private WebView webView;
    private TextView status;
    private boolean pageFailed;
    private final Runnable reportState = new Runnable() {
        @Override public void run() {
            if (webView != null && allowed(webView.getUrl())) {
                webView.evaluateJavascript(
                    "(function(){return JSON.stringify({url:location.href,isSecureContext:window.isSecureContext,"
                    + "navigatorGpu:!!navigator.gpu,navigatorGpuType:typeof navigator.gpu,coinRenderWebGPU:window.coinRenderWebGPU||null});})()",
                    value -> {
                        if (webView != null && allowed(webView.getUrl())) Log.i(TAG, "PAGE_STATE " + value);
                    });
            }
            handler.postDelayed(this, 3000);
        }
    };

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        status = new TextView(this);
        status.setTextSize(16);
        status.setMaxLines(5);
        status.setTextIsSelectable(true);
        layout.addView(status, new LinearLayout.LayoutParams(-1, -2));
        setContentView(layout);
        // Android 6 ignores Network Security Config. Fail closed rather than
        // loading with its system CA set instead of this app's exact CA policy.
        if (Build.VERSION.SDK_INT < 24) {
            error("Android 7/API 24 ou superior é necessário para confiança TLS restrita por aplicativo.");
            return;
        }
        Button reload = new Button(this);
        reload.setText("Recarregar site HTTPS fixo");
        layout.addView(reload, new LinearLayout.LayoutParams(-1, -2));
        webView = new WebView(this);
        layout.addView(webView, new LinearLayout.LayoutParams(-1, 0, 1));
        boolean debugBuild = (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        WebView.setWebContentsDebuggingEnabled(debugBuild);
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setAllowFileAccessFromFileURLs(false);
        settings.setAllowUniversalAccessFromFileURLs(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setJavaScriptCanOpenWindowsAutomatically(false);
        settings.setSupportMultipleWindows(false);
        webView.setWebViewClient(new WebViewClient() {
            @Override public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                return blockNavigation(request.getUrl().toString());
            }
            @Override public boolean shouldOverrideUrlLoading(WebView view, String url) {
                return blockNavigation(url);
            }
            @Override public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                return allowed(request.getUrl().toString()) ? null : blockedResponse();
            }
            @Override public void onPageStarted(WebView view, String url, Bitmap icon) {
                if (!allowed(url)) {
                    view.stopLoading();
                    error("Navegação bloqueada: origem diferente do HTTPS configurado.");
                } else if (!pageFailed) {
                    status.setText("Carregando " + ViewerConfig.URL + " — WebGPU depende do WebView instalado.");
                }
            }
            @Override public void onPageFinished(WebView view, String url) {
                if (allowed(url) && !pageFailed) status.setText(
                    "Documento HTTPS carregado. Estado real de WebGPU na página e no logcat; carregamento não comprova renderização.");
            }
            @Override public void onReceivedSslError(WebView view, SslErrorHandler sslHandler, SslError sslError) {
                sslHandler.cancel();
                error("TLS recusado pelo WebView: " + sslError.toString());
            }
            @Override public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError failure) {
                String message = "Erro WebView " + failure.getErrorCode() + ": " + failure.getDescription();
                if (request.isForMainFrame()) error(message);
                else Log.w(TAG, message + " no recurso " + request.getUrl());
            }
            @Override public void onReceivedHttpError(WebView view, WebResourceRequest request, WebResourceResponse response) {
                String message = "HTTP " + response.getStatusCode() + " no recurso " + request.getUrl();
                if (request.isForMainFrame()) error(message);
                else Log.w(TAG, message);
            }
        });
        webView.setWebChromeClient(new WebChromeClient() {
            @Override public boolean onConsoleMessage(ConsoleMessage message) {
                Log.i(TAG, "PAGE_CONSOLE " + message.message());
                return true;
            }
        });
        // Worker fetches obey the same origin boundary as document resources.
        ServiceWorkerController.getInstance().setServiceWorkerClient(new ServiceWorkerClient() {
            @Override public WebResourceResponse shouldInterceptRequest(WebResourceRequest request) {
                return allowed(request.getUrl().toString()) ? null : blockedResponse();
            }
        });
        reload.setOnClickListener(view -> loadFixedSite());
        loadFixedSite();
        handler.post(reportState);
    }

    private void loadFixedSite() {
        pageFailed = false;
        status.setText("Carregando site HTTPS configurado: " + ViewerConfig.URL);
        webView.loadUrl(ViewerConfig.URL);
    }

    private boolean blockNavigation(String url) {
        if (allowed(url)) return false;
        error("Navegação bloqueada: somente a origem HTTPS configurada é permitida.");
        return true;
    }

    private static boolean allowed(String value) {
        if (value == null) return false;
        try {
            Uri uri = Uri.parse(value);
            int port = uri.getPort() == -1 ? 443 : uri.getPort();
            return "https".equalsIgnoreCase(uri.getScheme()) && uri.getUserInfo() == null
                && ViewerConfig.HOST.equalsIgnoreCase(uri.getHost()) && port == ViewerConfig.PORT;
        } catch (RuntimeException failure) {
            return false;
        }
    }

    private static WebResourceResponse blockedResponse() {
        byte[] message = "Blocked: resource outside configured HTTPS origin".getBytes(StandardCharsets.UTF_8);
        return new WebResourceResponse("text/plain", "UTF-8", 403, "Forbidden", null,
                new ByteArrayInputStream(message));
    }

    private void error(String message) {
        pageFailed = true;
        Log.e(TAG, message);
        runOnUiThread(() -> status.setText(message));
    }

    @Override protected void onDestroy() {
        handler.removeCallbacks(reportState);
        if (webView != null) {
            webView.stopLoading();
            webView.destroy();
            webView = null;
        }
        super.onDestroy();
    }
}
