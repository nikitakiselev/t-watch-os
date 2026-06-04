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
    String h =
        "<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>watchOS radio</title></head>"
        "<body style='font-family:sans-serif;background:#111;color:#3f6;max-width:640px;margin:auto;padding:14px'>"
        "<h2>Radio stations</h2>"
        "<p style='color:#9c9'>One stream URL per line. Lines starting with # are ignored.</p>"
        "<form method=POST action=/save>"
        "<textarea name=list spellcheck=false "
        "style='width:100%;height:300px;background:#000;color:#3f6;font-size:14px;border:1px solid #3f6'>";
    h += htmlEscape(readStations());
    h += "</textarea><br><br>"
         "<button type=submit style='font-size:17px;padding:9px 22px;background:#3f6;color:#000;border:0'>Save</button>"
         "</form>"
         "<form method=POST action=/stop style='margin-top:18px'>"
         "<button type=submit style='font-size:14px;padding:7px 16px;background:#511;color:#f88;border:0'>Stop server</button>"
         "</form></body></html>";
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
