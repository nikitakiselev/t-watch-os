#include "webedit.h"
#include "stations.h"
#include <WebServer.h>
#include <WiFi.h>
#include <SPIFFS.h>

static WebServer *srv = nullptr;
static bool       pendingStop = false;

static String readStations()
{
    String s;
    File f = SPIFFS.open("/stations.txt", "r");
    if (f) { while (f.available()) s += (char)f.read(); f.close(); }
    return s;
}

static String htmlEscape(const String &in)
{
    String o; o.reserve(in.length() + 16);
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if      (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else               o += c;
    }
    return o;
}

static void handleRoot()
{
    // Нумерованный жёлоб (gutter) слева от textarea: нумеруются только строки-
    // станции (непустые и не начинающиеся с #), пустые/комментарии — без номера.
    // Номера — чисто визуальные, в textarea и в файл не попадают. wrap=off, чтобы
    // длинные URL не переносились и номера оставались напротив своих строк.
    String h =
        "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>watchOS radio</title>"
        "<style>"
        "body{font-family:sans-serif;background:#111;color:#3f6;max-width:640px;margin:auto;padding:14px}"
        ".wrap{display:flex;height:300px;border:1px solid #3f6;background:#000}"
        "#gut{margin:0;padding:8px 6px;min-width:30px;line-height:20px;font:14px/20px monospace;"
        "color:#9c9;text-align:right;white-space:pre;overflow:hidden;border-right:1px solid #243}"
        "#list{flex:1;margin:0;padding:8px;line-height:20px;font:14px/20px monospace;background:#000;"
        "color:#3f6;border:0;outline:0;resize:none;white-space:pre;overflow:auto}"
        "</style></head>"
        "<body>"
        "<h2>Radio stations</h2>"
        "<p style='color:#9c9'>One stream URL per line. Lines starting with # are ignored.</p>"
        "<form method=POST action=/save>"
        "<div class=wrap><pre id=gut></pre>"
        "<textarea id=list name=list spellcheck=false wrap=off>";
    h += htmlEscape(readStations());
    h += "</textarea></div><br>"
         "<button type=submit style='font-size:17px;padding:9px 22px;background:#3f6;color:#000;border:0'>Save</button>"
         "</form>"
         "<form method=POST action=/stop style='margin-top:18px'>"
         "<button type=submit style='font-size:14px;padding:7px 16px;background:#511;color:#f88;border:0'>Stop server</button>"
         "</form>"
         "<script>"
         "var ta=document.getElementById('list'),g=document.getElementById('gut');"
         "function upd(){var L=ta.value.split('\\n'),n=0,o=[];"
         "for(var i=0;i<L.length;i++){var t=L[i].trim();"
         "if(t&&t[0]!='#'){o.push(++n);}else{o.push('');}}"
         "g.textContent=o.join('\\n');}"
         "ta.addEventListener('input',upd);"
         "ta.addEventListener('scroll',function(){g.scrollTop=ta.scrollTop;});"
         "upd();"
         "</script>"
         "</body></html>";
    srv->send(200, "text/html", h);
}

static void handleSave()
{
    if (srv->hasArg("list")) {
        File f = SPIFFS.open("/stations.txt", "w");
        if (f) { f.print(srv->arg("list")); f.close(); stationsReload(); }
    }
    srv->sendHeader("Location", "/");
    srv->send(303, "text/plain", "saved");
}

static void handleStop()
{
    srv->send(200, "text/html",
              "<body style='background:#111;color:#3f6;font-family:sans-serif;padding:14px'>"
              "Server stopped. You can close this page.</body>");
    pendingStop = true;                 // фактический стоп — в webEditTick (после отправки ответа)
}

void webEditStart()
{
    if (srv) return;
    srv = new WebServer(80);
    srv->on("/",     HTTP_GET,  handleRoot);
    srv->on("/save", HTTP_POST, handleSave);
    srv->on("/stop", HTTP_POST, handleStop);
    srv->begin();
    pendingStop = false;
}

void webEditStop()
{
    if (!srv) return;
    srv->stop();
    delete srv;
    srv = nullptr;
}

bool webEditRunning() { return srv != nullptr; }

void webEditTick()
{
    if (!srv) return;
    srv->handleClient();
    if (pendingStop) webEditStop();
}

String webEditIP() { return WiFi.localIP().toString(); }
