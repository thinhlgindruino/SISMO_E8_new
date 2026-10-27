"""
tb_setup.py - Cài sẵn mọi thứ E8+ cần trên server ThingsBoard (CE 4.x) qua REST API

Tạo hoặc cập nhật (chạy lại nhiều lần được):
  - Device profile "E8+" bật tự đăng ký (provision key / secret phải trùng CLOUD_PROVISION_KEY / SECRET trong
    firmware, components/cloud/include/cloud.h)
  - Widget "E8+ Control" (fqn e8plus_control, thư mục widget/) trong bundle "E8+ Sismo"
  - Dashboard "E8+ Sismo": trang danh sách máy -> bấm 1 dòng -> trang điều khiển máy đó + lịch sử
  - --customer "Tên khách": gán dashboard cho khách đó (khách đăng nhập chỉ thấy máy của mình)
  - --export <thư mục>: ghi thêm widget và dashboard ra file JSON (để Import bằng tay trên giao diện)

Chạy:  pip install requests
       python tb_setup.py --url http://localhost:8080 --user tenant@thingsboard.org --password tenant
"""
import argparse, base64, json, os, sys, uuid
import requests

HERE = os.path.dirname(os.path.abspath(__file__))
PROFILE = "E8+"
PROVISION_KEY = "e8plus-provision-key"
PROVISION_SECRET = "e8plus-provision-secret"
WIDGET_FQN = "e8plus_control"
BUNDLE_ALIAS = "e8plus_sismo"
DASHBOARD = "E8+ Sismo"

CONTROL_ATTRS = ["firmware", "access_mode", "motor_mode", "card_timer", "language", "motor_ratio", "remote_lock",
                 "sessions_total", "mac", "ip", "ssid", "active", "lastConnectTime", "lastDisconnectTime",
                 "customer_name", "customer_info", "customer_notes"]
CONTROL_TS = ["state", "kind", "remaining_s", "planned_s", "program", "fault", "rssi", "fw_state", "fw_progress", "fw_error"]


class TB:
    def __init__(self, url, user, password):
        self.url = url.rstrip("/")
        r = requests.post(self.url + "/api/auth/login", json={"username": user, "password": password}, timeout=20)
        r.raise_for_status()
        self.h = {"X-Authorization": "Bearer " + r.json()["token"]}

    def req(self, method, path, **kw):
        r = requests.request(method, self.url + path, headers=self.h, timeout=30, **kw)
        if r.status_code >= 400:
            raise RuntimeError("%s %s -> %d %s" % (method, path, r.status_code, r.text[:300]))
        return r.json() if r.text else None

    def find(self, path, key, value):
        page = 0
        while True:
            r = self.req("GET", "%s%spageSize=100&page=%d" % (path, "&" if "?" in path else "?", page))
            for x in r["data"]:
                if x.get(key) == value:
                    return x
            if not r.get("hasNext"):
                return None
            page += 1


def key(name, ktype, label, settings=None, extra=None):
    k = {"name": name, "type": ktype, "label": label, "color": "#2196f3", "settings": settings or {}, "_hash": 0.5}
    if extra:
        k.update(extra)
    return k


def setup_profile(tb):
    p = tb.find("/api/deviceProfiles", "name", PROFILE)
    body = p and tb.req("GET", "/api/deviceProfile/" + p["id"]["id"]) or {
        "name": PROFILE, "type": "DEFAULT", "transportType": "MQTT",
        "profileData": {"configuration": {"type": "DEFAULT"}, "transportConfiguration": {"type": "MQTT"}}}
    body["description"] = "Sismo Fitness E8+ (ESP32, firmware e8_rtos)"
    body["provisionType"] = "ALLOW_CREATE_NEW_DEVICES"
    body["provisionDeviceKey"] = PROVISION_KEY
    body["profileData"]["provisionConfiguration"] = {"type": "ALLOW_CREATE_NEW_DEVICES", "provisionDeviceSecret": PROVISION_SECRET}
    saved = tb.req("POST", "/api/deviceProfile", json=body)
    print("Device profile %s: %s" % (PROFILE, saved["id"]["id"]))
    return saved


def widget_descriptor():
    read = lambda n: open(os.path.join(HERE, "widget", n), encoding="utf-8").read()
    logo = base64.b64encode(open(os.path.join(HERE, "widget", "logo.png"), "rb").read()).decode()
    html = read("e8_control.html").replace('<img class="e8w-logo"', '<img class="e8w-logo" src="data:image/png;base64,%s"' % logo)
    default_config = {
        "datasources": [{"type": "function", "name": "function", "dataKeys": [key("state", "function", "state", extra={"funcBody": "return 'IDLE';"})]}],
        "showTitle": False, "backgroundColor": "#0e111a", "color": "#ffffff", "padding": "0px", "settings": {},
        "title": "E8+ Control", "dropShadow": True, "enableFullscreen": True}
    return {"type": "latest", "sizeX": 6, "sizeY": 12, "resources": [], "templateHtml": html,
            "templateCss": read("e8_control.css"), "controllerScript": read("e8_control.js"),
            "settingsSchema": "{}", "dataKeySettingsSchema": "{}", "defaultConfig": json.dumps(default_config)}


def setup_widget(tb):
    old = tb.find("/api/widgetTypes?tenantOnly=true&fullSearch=false", "fqn", WIDGET_FQN)
    body = tb.req("GET", "/api/widgetType/" + old["id"]["id"]) if old else {"fqn": WIDGET_FQN}
    body.update({"name": "E8+ Control", "description": "Bang dieu khien may Sismo E8+", "descriptor": widget_descriptor()})
    w = tb.req("POST", "/api/widgetType", json=body)
    b = tb.find("/api/widgetsBundles?tenantOnly=true", "alias", BUNDLE_ALIAS)
    if not b:
        b = tb.req("POST", "/api/widgetsBundle", json={"alias": BUNDLE_ALIAS, "title": "E8+ Sismo", "description": "Widgets Sismo E8+"})
    tb.req("POST", "/api/widgetsBundle/%s/widgetTypeFqns" % b["id"]["id"], json=[WIDGET_FQN])
    print("Widget %s: %s (bundle %s)" % (WIDGET_FQN, w["id"]["id"], b["title"]))
    return w, b


def grid(rows_h=70):
    return {"backgroundColor": "#eeeeee", "columns": 24, "margin": 10, "outerMargin": True, "backgroundSizeMode": "100%",
            "autoFillHeight": False, "mobileAutoFillHeight": False, "mobileRowHeight": rows_h, "layoutType": "default"}


def base_config(title, datasources, settings, show_title=True):
    return {"datasources": datasources, "timewindow": {"realtime": {"timewindowMs": 86400000}},
            "showTitle": show_title, "title": title, "backgroundColor": "#ffffff", "color": "rgba(0,0,0,0.87)",
            "padding": "4px", "settings": settings, "dropShadow": True, "enableFullscreen": True,
            "titleStyle": {"fontSize": "16px", "fontWeight": 400}, "useDashboardTimewindow": False,
            "showLegend": False, "actions": {}, "widgetStyle": {}}


def dashboard_config():
    alias_all, alias_one = str(uuid.uuid4()), str(uuid.uuid4())
    w_list, w_ctrl, w_hist = str(uuid.uuid4()), str(uuid.uuid4()), str(uuid.uuid4())
    dot = {"useCellContentFunction": True, "useCellStyleFunction": True, "columnWidth": "60px",
           "cellContentFunction": "return '&#11044;';",
           "cellStyleFunction": "return {color: value === 'true' || value === true ? '#26b33a' : '#b0b0b0', fontSize: '18px'};"}
    fault = {"useCellContentFunction": True,
             "cellContentFunction": "return value && value != '0' ? '<span style=\"color:#d32f2f\">ERR.0' + value + '</span>' : 'OK';"}
    lst = {"type": "latest", "sizeX": 24, "sizeY": 10, "typeFullFqn": "system.cards.entities_table",
           "config": base_config("Machines E8+", [{"type": "entity", "entityAliasId": alias_all, "dataKeys": [
               key("active", "attribute", "En ligne", dot),
               key("customer_name", "attribute", "Client"),
               key("firmware", "attribute", "Firmware"),
               key("state", "timeseries", "État"),
               key("program", "timeseries", "Programme"),
               key("fault", "timeseries", "Défaut", fault),
               key("sessions_total", "attribute", "Séances"),
               key("customer_notes", "attribute", "Notes")]}],
               {"enableSearch": True, "displayPagination": True, "defaultPageSize": 20, "defaultSortOrder": "entityName",
                "displayEntityName": True, "entityNameColumnTitle": "Machine", "displayEntityLabel": False,
                "displayEntityType": False, "entitiesTitle": "Machines E8+", "enableSelectColumnDisplay": True})}
    lst["config"]["actions"] = {"rowClick": [{"id": str(uuid.uuid4()), "name": "Ouvrir", "icon": "more_horiz",
                                              "type": "openDashboardState", "targetDashboardStateId": "machine",
                                              "setEntityId": True, "stateEntityParamName": None}]}
    ctrl_keys = [key(k, "attribute", k) for k in CONTROL_ATTRS] + [key(k, "timeseries", k) for k in CONTROL_TS]
    ctrl = {"type": "latest", "sizeX": 7, "sizeY": 19, "typeFullFqn": "tenant." + WIDGET_FQN,
            "config": base_config("E8+", [{"type": "entity", "entityAliasId": alias_one, "dataKeys": ctrl_keys}], {}, False)}
    ctrl["config"].update({"backgroundColor": "#0e111a", "color": "#ffffff", "padding": "0px", "enableFullscreen": False})
    hist = {"type": "timeseries", "sizeX": 17, "sizeY": 19, "typeFullFqn": "system.cards.timeseries_table",
            "config": base_config("Historique (7 jours)", [{"type": "entity", "entityAliasId": alias_one, "dataKeys": [
                key("event", "timeseries", "Événement"), key("session_kind", "timeseries", "Type"),
                key("session_program", "timeseries", "Programme"), key("session_level", "timeseries", "Niveau"),
                key("session_done_s", "timeseries", "Durée (s)"), key("error_code", "timeseries", "Erreur"),
                key("fw_state", "timeseries", "Firmware")]}],
                {"showTimestamp": True, "displayPagination": True, "defaultPageSize": 15, "enableSearch": True,
                 "hideEmptyLines": True, "showMilliseconds": False})}
    hist["config"]["timewindow"] = {"realtime": {"timewindowMs": 7 * 86400000}, "aggregation": {"type": "NONE", "limit": 500}}
    for i, w in ((w_list, lst), (w_ctrl, ctrl), (w_hist, hist)):
        w["id"] = i
    return {
        "description": "Sismo Fitness E8+",
        "widgets": {w_list: lst, w_ctrl: ctrl, w_hist: hist},
        "states": {
            "default": {"name": "Machines E8+", "root": True, "layouts": {"main": {
                "widgets": {w_list: {"sizeX": 24, "sizeY": 10, "row": 0, "col": 0}}, "gridSettings": grid()}}},
            "machine": {"name": "${entityName}", "root": False, "layouts": {"main": {
                "widgets": {w_ctrl: {"sizeX": 7, "sizeY": 19, "row": 0, "col": 0, "mobileOrder": 1, "mobileHeight": 14},
                            w_hist: {"sizeX": 17, "sizeY": 19, "row": 0, "col": 7, "mobileOrder": 2, "mobileHeight": 8}},
                "gridSettings": grid()}}}},
        "entityAliases": {
            alias_all: {"id": alias_all, "alias": "Machines E8+", "filter": {"type": "deviceType", "resolveMultiple": True,
                                                                              "deviceTypes": [PROFILE], "deviceNameFilter": ""}},
            alias_one: {"id": alias_one, "alias": "Machine", "filter": {"type": "stateEntity", "resolveMultiple": False,
                                                                         "stateEntityParamName": None, "defaultStateEntity": None}}},
        "filters": {},
        "timewindow": {"realtime": {"timewindowMs": 86400000}},
        "settings": {"stateControllerId": "entity", "showTitle": False, "showDashboardsSelect": False,
                     "showEntitiesSelect": False, "showDashboardTimewindow": False, "showDashboardExport": False,
                     "toolbarAlwaysOpen": True, "titleColor": "rgba(0,0,0,0.870588)"}}


def setup_dashboard(tb):
    d = tb.find("/api/tenant/dashboards", "title", DASHBOARD)
    body = d and tb.req("GET", "/api/dashboard/" + d["id"]["id"]) or {"title": DASHBOARD}
    body["configuration"] = dashboard_config()
    saved = tb.req("POST", "/api/dashboard", json=body)
    print("Dashboard %s: %s/dashboards/%s" % (DASHBOARD, tb.url, saved["id"]["id"]))
    return saved


def assign(tb, dash, customer_title):
    c = tb.find("/api/customers", "title", customer_title)
    if not c:
        c = tb.req("POST", "/api/customer", json={"title": customer_title})
        print("Tao khach hang %s" % customer_title)
    tb.req("POST", "/api/customer/%s/dashboard/%s" % (c["id"]["id"], dash["id"]["id"]))
    print("Gan dashboard cho khach %s" % customer_title)


def export(tb, widget, bundle, dash, folder):
    os.makedirs(folder, exist_ok=True)
    strip = lambda o: {k: v for k, v in o.items() if k not in ("id", "createdTime", "tenantId", "externalId", "version")}
    wt = strip(widget)
    json.dump(wt, open(os.path.join(folder, "e8plus_control_widget.json"), "w", encoding="utf-8"), indent=2, ensure_ascii=False)
    json.dump({"widgetsBundle": strip(bundle), "widgetTypes": [wt]},
              open(os.path.join(folder, "e8plus_widgets_bundle.json"), "w", encoding="utf-8"), indent=2, ensure_ascii=False)
    full = tb.req("GET", "/api/dashboard/" + dash["id"]["id"])
    out = strip(full)
    out.pop("assignedCustomers", None)
    json.dump(out, open(os.path.join(folder, "e8plus_dashboard.json"), "w", encoding="utf-8"), indent=2, ensure_ascii=False)
    print("Da xuat JSON vao %s" % folder)


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument("--url", default="http://localhost:8080")
    a.add_argument("--user", default="tenant@thingsboard.org")
    a.add_argument("--password", default="tenant")
    a.add_argument("--customer", action="append", default=[])
    a.add_argument("--export")
    o = a.parse_args()
    tb = TB(o.url, o.user, o.password)
    setup_profile(tb)
    w, b = setup_widget(tb)
    dash = setup_dashboard(tb)
    for c in o.customer:
        assign(tb, dash, c)
    if o.export:
        export(tb, w, b, dash, o.export)


if __name__ == "__main__":
    main()
