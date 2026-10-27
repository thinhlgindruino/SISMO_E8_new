"""
e8_sim.py - Máy E8+ giả (chạy trên máy tính) nói chuyện với ThingsBoard giống hệt firmware (components/cloud)

Dùng để demo / thử dashboard khi chưa cắm ESP32: tự đăng ký (provision), gửi attributes + trạng thái,
trả lời RPC play / pause / stop / setRemoteLock / setSettings / getStatus / reboot, tải firmware OTA theo khúc
(kiểm SHA-256, "cài" bằng cách đổi số phiên bản). Buổi tập Manuel giả: Play -> đếm ngược 5 phút.
Gõ lệnh trong cửa sổ đang chạy: error 3 | error 0 | quit

Chạy:  pip install paho-mqtt
       python e8_sim.py --host localhost --name SISMO_E8_SIM001
"""
import argparse, hashlib, json, threading, time
import paho.mqtt.client as mqtt

PROVISION_KEY = "e8plus-provision-key"
PROVISION_SECRET = "e8plus-provision-secret"
CHUNK = 4096


class Machine:
    def __init__(self, name, version):
        self.name = name
        self.version = version
        self.token = hashlib.sha256(("%s:%s" % (PROVISION_SECRET, name)).encode()).hexdigest()[:20]
        self.settings = {"access_mode": 0, "motor_mode": 3, "card_timer": 0, "language": 0, "motor_ratio": 9,
                         "control_mode": 1, "manual_minutes": 5, "remote_lock": False}
        self.state, self.remaining, self.planned, self.fault, self.sessions = "IDLE", 0, 0, 0, 0
        self.fw = None
        self.lock = threading.Lock()
        self.client = None
        self.connected = threading.Event()

    def pub(self, topic, obj, qos=1):
        self.client.publish(topic, obj if isinstance(obj, str) else json.dumps(obj), qos)

    def attributes(self):
        a = {"model": "E8+", "firmware": self.version, "device_name": self.name, "mac": "SIM", "ip": "127.0.0.1",
             "ssid": "simulator", "sessions_total": self.sessions}
        a.update(self.settings)
        self.pub("v1/devices/me/attributes", a)

    def status(self):
        self.pub("v1/devices/me/telemetry", {"state": self.state, "kind": "manual" if self.planned else "",
                 "remaining_s": self.remaining, "planned_s": self.planned, "intensity": 10, "program": "",
                 "level": 0, "page": 10 if self.planned else 1, "cm8": True, "card": True, "fault": self.fault,
                 "rssi": -50, "uptime_s": int(time.monotonic()), "heap": 100000}, 0)

    def provision(self, host, port):
        done = threading.Event()
        c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=self.name)
        c.username_pw_set("provision")
        c.on_connect = lambda cl, u, f, rc, p=None: (cl.subscribe("/provision/response", 1), cl.publish("/provision/request", json.dumps({
            "deviceName": self.name, "provisionDeviceKey": PROVISION_KEY, "provisionDeviceSecret": PROVISION_SECRET,
            "credentialsType": "ACCESS_TOKEN", "token": self.token}), 1))
        c.on_message = lambda cl, u, m: (print("Dang ky:", m.payload.decode()), done.set())
        c.connect(host, port, 30)
        c.loop_start()
        done.wait(15)
        c.loop_stop()
        c.disconnect()

    def run(self, host, port):
        refused = threading.Event()
        def on_connect(cl, u, flags, rc, props=None):
            if rc != 0:
                refused.set()
                return
            for t in ("v1/devices/me/attributes", "v1/devices/me/attributes/response/+", "v1/devices/me/rpc/request/+", "v2/fw/response/+/chunk/+"):
                cl.subscribe(t, 1)
            self.attributes()
            self.pub("v1/devices/me/telemetry", {"current_fw_title": "E8+", "current_fw_version": self.version})
            self.status()
            self.pub("v1/devices/me/attributes/request/1", {"sharedKeys": "fw_title,fw_version,fw_size,fw_checksum,fw_checksum_algorithm"})
            self.connected.set()
            print("ONLINE:", self.name, "v" + self.version)
        for attempt in range(2):
            refused.clear()
            self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=self.name)
            self.client.username_pw_set(self.token)
            self.client.on_connect = on_connect
            self.client.on_message = self.on_message
            self.client.connect(host, port, 30)
            self.client.loop_start()
            time.sleep(3)
            if not refused.is_set():
                return
            self.client.loop_stop()
            print("Server chua co thiet bi -> dang ky")
            self.provision(host, port)

    def rpc(self, method, params):
        locked = self.settings["remote_lock"]
        if method in ("play", "pause", "stop", "setSettings", "reboot") and locked:
            return "locked"
        if method == "play":
            if self.state == "IDLE":
                self.planned = self.remaining = self.settings["manual_minutes"] * 60
            self.state = "RUN"
        elif method == "pause":
            if self.state != "RUN":
                return "not_running"
            self.state = "PAUSE"
        elif method == "stop":
            if self.state != "IDLE":
                self.end_session()
        elif method == "setRemoteLock":
            self.settings["remote_lock"] = bool(params.get("locked")) if isinstance(params, dict) else bool(params)
        elif method == "setSettings":
            for k in ("access_mode", "motor_mode", "card_timer", "language", "motor_ratio"):
                if k in params:
                    self.settings[k] = int(params[k])
        elif method == "reboot":
            if self.state != "IDLE":
                return "busy"
        elif method != "getStatus":
            return "unknown_method"
        return None

    def end_session(self):
        self.pub("v1/devices/me/telemetry", {"event": "session_end", "session_kind": "manual",
                 "session_planned_s": self.planned, "session_done_s": self.planned - self.remaining,
                 "session_completed": self.remaining <= 1})
        self.state, self.remaining, self.planned = "IDLE", 0, 0
        self.sessions += 1
        self.attributes()

    def on_message(self, cl, u, m):
        t = m.topic
        if t.startswith("v1/devices/me/rpc/request/"):
            req = json.loads(m.payload)
            with self.lock:
                was = self.state
                err = self.rpc(req.get("method", ""), req.get("params") or {})
                if was == "IDLE" and self.state == "RUN":
                    self.pub("v1/devices/me/telemetry", {"event": "session_start", "session_kind": "manual", "session_planned_s": self.planned})
                res = {"ok": err is None, "state": self.state, "page": 10 if self.planned else 1}
                if err:
                    res["error"] = err
                res.update(self.settings)
                self.pub("v1/devices/me/rpc/response/" + t.split("/")[-1], res)
                self.attributes()
                self.status()
            print("RPC", req.get("method"), "->", err or "OK")
        elif t == "v1/devices/me/attributes" or t.startswith("v1/devices/me/attributes/response/"):
            a = json.loads(m.payload)
            a = a.get("shared", a) if not t.endswith("attributes") else a
            if "fw_version" in a or "fw_checksum" in a:
                if t.endswith("attributes"):
                    self.pub("v1/devices/me/attributes/request/2", {"sharedKeys": "fw_title,fw_version,fw_size,fw_checksum,fw_checksum_algorithm"})
                elif a.get("fw_title") == "E8+" and a.get("fw_version") != self.version:
                    self.fw = {"version": a["fw_version"], "size": a["fw_size"], "checksum": a["fw_checksum"],
                               "id": int(time.time()) % 100000, "chunk": 0, "data": b""}
                    print("Firmware moi", a["fw_version"], "-> tai")
                    self.pub("v1/devices/me/telemetry", {"fw_state": "DOWNLOADING"})
                    self.pub("v2/fw/request/%d/chunk/0" % self.fw["id"], str(CHUNK))
        elif t.startswith("v2/fw/response/") and self.fw:
            self.fw["data"] += m.payload
            self.fw["chunk"] += 1
            pct = len(self.fw["data"]) * 100 // max(1, self.fw["size"])
            self.pub("v1/devices/me/telemetry", {"fw_progress": pct}, 0)
            if m.payload and len(self.fw["data"]) < self.fw["size"]:
                self.pub("v2/fw/request/%d/chunk/%d" % (self.fw["id"], self.fw["chunk"]), str(CHUNK))
                return
            ok = hashlib.sha256(self.fw["data"]).hexdigest() == self.fw["checksum"].lower()
            for s in ("DOWNLOADED", "VERIFIED", "UPDATING") if ok else ("DOWNLOADED",):
                self.pub("v1/devices/me/telemetry", {"fw_state": s})
                time.sleep(0.3)
            if not ok:
                self.pub("v1/devices/me/telemetry", {"fw_state": "FAILED", "fw_error": "checksum mismatch"})
            else:
                self.version = self.fw["version"]
                print("Da cai firmware v" + self.version + " (gia lap khoi dong lai)")
                self.pub("v1/devices/me/telemetry", {"fw_state": "UPDATED", "current_fw_title": "E8+", "current_fw_version": self.version})
                self.attributes()
            self.fw = None

    def tick(self):
        last = 0
        while True:
            time.sleep(1)
            with self.lock:
                if self.state == "RUN":
                    self.remaining -= 1
                    if self.remaining <= 0:
                        self.end_session()
                if self.connected.is_set() and (self.state == "RUN" and self.remaining % 5 == 0 or time.time() - last >= 30):
                    self.status()
                    last = time.time()


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--host", default="localhost")
    a.add_argument("--port", type=int, default=1883)
    a.add_argument("--name", default="SISMO_E8_SIM001")
    a.add_argument("--version", default="1.13.0")
    o = a.parse_args()
    m = Machine(o.name, o.version)
    m.run(o.host, o.port)
    threading.Thread(target=m.tick, daemon=True).start()
    while True:
        line = input().strip()
        if line.startswith("error"):
            m.fault = int(line.split()[1])
            if m.fault:
                m.pub("v1/devices/me/telemetry", {"event": "error", "error_code": "ERR.0%d" % m.fault})
            m.status()
        elif line == "quit":
            break


if __name__ == "__main__":
    main()
