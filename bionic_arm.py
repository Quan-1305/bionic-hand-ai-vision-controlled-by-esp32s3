"""
bionic_arm.py - Dieu khien 5 servo bang ban tay (MediaPipe)
Ban toi uu: do tre thap, 5 muc goc, chong nhieu khi xoay tay.
"""
import os
import json
import time
import socket
import threading

import cv2
import numpy as np
import requests
import mediapipe as mp

# ===================== CAU HINH =====================
ESP_IP = "192.168.1.2"      # IP that cua ESP32 (khong co http://)
USE_UDP = False              # True: UDP (nhanh nhat, can sua firmware) | False: HTTP /servo
UDP_PORT = 4210
CAM_INDEX = 0

# 5 muc goc: index 0 = ngon NAM, index 4 = ngon DUOI
LEVELS = [0, 45, 90, 135, 180]
# Dao chieu tung servo neu quay nguoc: [Cai, Tro, Giua, ApUt, Ut]
INVERT = [False, False, False, False, False]

HYSTERESIS = 0.15      # vung dem chong nhay muc (0.1 - 0.3). Cang lon cang on dinh
CONFIRM_FRAMES = 2     # so frame lien tiep phai thay muc moi moi doi (1 = nhanh nhat)
FEATURE_ALPHA = 0.6    # loc muot dac trung (1.0 = tat loc, nho hon = muot hon nhung tre hon)
KEEPALIVE = 0.25       # (UDP) gui lai trang thai moi 0.25s de bu goi bi mat
MODEL_COMPLEXITY = 1   # 0 = nhanh hon nua nhung de nhieu khi xoay tay, 1 = on dinh hon
DRAW_SKELETON = True   # ve khung xuong ban tay
CALIB_FILE = "calib.json"

FINGER_NAMES = ["Cai", "Tro", "Giua", "Ap ut", "Ut"]

# Gia tri mac dinh. Hieu chinh bang phim O / F (se tu luu vao calib.json)
# Ngon cai: ti le khoang cach 3D | 4 ngon con lai: tong goc gap 3 khop (do)
OPEN_VAL = np.array([1.70, 25.0, 25.0, 25.0, 25.0])
CLOSED_VAL = np.array([0.80, 210.0, 210.0, 210.0, 210.0])
# ====================================================

mp_hands = mp.solutions.hands
mp_draw = mp.solutions.drawing_utils
FONT = cv2.FONT_HERSHEY_SIMPLEX


# ---------- Camera: luong rieng, luon giu frame moi nhat ----------
class Camera:
    def __init__(self, index):
        backend = cv2.CAP_DSHOW if os.name == "nt" else cv2.CAP_ANY
        self.cap = cv2.VideoCapture(index, backend)
        self.cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        self.cap.set(cv2.CAP_PROP_FPS, 30)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.opened = self.cap.isOpened()
        self.frame = None
        self.ready = threading.Event()
        self.running = True
        threading.Thread(target=self._loop, daemon=True).start()

    def _loop(self):
        while self.running:
            ok, f = self.cap.read()
            if ok:
                self.frame = f
                self.ready.set()
            else:
                time.sleep(0.01)

    def read(self, timeout=2.0):
        if not self.ready.wait(timeout):
            return None
        self.ready.clear()
        return self.frame

    def release(self):
        self.running = False
        time.sleep(0.05)
        self.cap.release()


# ---------- Gui lenh toi ESP32 ----------
class Sender:
    def __init__(self):
        self.enabled = True
        self.last = None
        self.last_t = 0.0
        if USE_UDP:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.addr = (ESP_IP, UDP_PORT)
        else:
            self.session = requests.Session()
            self.pending = None
            self.event = threading.Event()
            self.running = True
            threading.Thread(target=self._http_loop, daemon=True).start()

    def send(self, angles):
        if not self.enabled:
            return
        angles = tuple(int(a) for a in angles)
        now = time.time()
        unchanged = angles == self.last
        if unchanged and (not USE_UDP or now - self.last_t < KEEPALIVE):
            return
        self.last = angles
        self.last_t = now
        if USE_UDP:
            try:
                self.sock.sendto(bytes(angles), self.addr)
            except OSError as e:
                print("Loi UDP:", e)
        else:
            self.pending = angles
            self.event.set()

    def _http_loop(self):
        while self.running:
            self.event.wait()
            self.event.clear()
            a = self.pending
            params = {f"s{i + 1}": a[i] for i in range(5)}
            try:
                self.session.get(f"http://{ESP_IP}/servo", params=params, timeout=1)
            except requests.RequestException as e:
                print("Loi HTTP:", e)


# ---------- Dac trung ban tay (bat bien theo huong xoay) ----------
FINGER_CHAINS = [(5, 6, 7, 8), (9, 10, 11, 12), (13, 14, 15, 16), (17, 18, 19, 20)]


def angle_at(a, b, c):
    v1, v2 = a - b, c - b
    cos = np.dot(v1, v2) / (np.linalg.norm(v1) * np.linalg.norm(v2) + 1e-9)
    return np.degrees(np.arccos(np.clip(cos, -1.0, 1.0)))


def hand_features(P):
    """P: 21x3 toa do 3D (world landmarks, don vi met).
    Dung goc va ti le khoang cach 3D nen khong bi anh huong khi xoay tay."""
    f = np.zeros(5)
    palm = np.linalg.norm(P[5] - P[17]) + 1e-9
    f[0] = np.linalg.norm(P[4] - P[17]) / palm
    for i, (mcp, pip, dip, tip) in enumerate(FINGER_CHAINS, start=1):
        f[i] = ((180 - angle_at(P[0], P[mcp], P[pip]))
                + (180 - angle_at(P[mcp], P[pip], P[dip]))
                + (180 - angle_at(P[pip], P[dip], P[tip])))
    return f


def features_to_openness(f):
    den = OPEN_VAL - CLOSED_VAL
    den = np.where(np.abs(den) < 1e-6, 1e-6, den)
    return np.clip((f - CLOSED_VAL) / den, 0.0, 1.0)


# ---------- Chia 5 muc co vung dem (hysteresis) ----------
class Quantizer:
    def __init__(self):
        self.level = [None] * 5
        self.cand = [None] * 5
        self.count = [0] * 5

    def update(self, t):
        n = len(LEVELS) - 1
        for i in range(5):
            pos = t[i] * n
            if self.level[i] is None:
                self.level[i] = int(round(pos))
                continue
            if abs(pos - self.level[i]) <= 0.5 + HYSTERESIS:
                self.cand[i], self.count[i] = None, 0
                continue
            c = int(round(pos))
            if c == self.cand[i]:
                self.count[i] += 1
            else:
                self.cand[i], self.count[i] = c, 1
            if self.count[i] >= CONFIRM_FRAMES:
                self.level[i] = c
                self.cand[i], self.count[i] = None, 0
        return self.level


def levels_to_angles(levels):
    n = len(LEVELS) - 1
    return [LEVELS[n - l] if INVERT[i] else LEVELS[l] for i, l in enumerate(levels)]


# ---------- Luu / doc hieu chinh ----------
def load_calib():
    global OPEN_VAL, CLOSED_VAL
    if os.path.exists(CALIB_FILE):
        try:
            with open(CALIB_FILE) as fp:
                d = json.load(fp)
            OPEN_VAL = np.array(d["open"], dtype=float)
            CLOSED_VAL = np.array(d["closed"], dtype=float)
            print("Da nap hieu chinh tu", CALIB_FILE)
        except Exception as e:
            print("Khong doc duoc calib:", e)


def save_calib():
    with open(CALIB_FILE, "w") as fp:
        json.dump({"open": OPEN_VAL.tolist(), "closed": CLOSED_VAL.tolist()}, fp)


# ---------- Chuong trinh chinh ----------
def main():
    global OPEN_VAL, CLOSED_VAL
    load_calib()

    cam = Camera(CAM_INDEX)
    if not cam.opened:
        print("Khong mo duoc camera. Thu doi CAM_INDEX.")
        return
    sender = Sender()
    quant = Quantizer()

    smooth = None
    angles = None

    with mp_hands.Hands(
        static_image_mode=False,
        max_num_hands=1,
        model_complexity=MODEL_COMPLEXITY,
        min_detection_confidence=0.6,
        min_tracking_confidence=0.5,
    ) as hands:
        while True:
            frame = cam.read()
            if frame is None:
                print("Khong doc duoc camera")
                break

            frame = cv2.flip(frame, 1)
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            rgb.flags.writeable = False
            res = hands.process(rgb)

            has_hand = bool(res.multi_hand_landmarks and res.multi_hand_world_landmarks)
            if has_hand:
                if DRAW_SKELETON:
                    mp_draw.draw_landmarks(frame, res.multi_hand_landmarks[0],
                                           mp_hands.HAND_CONNECTIONS)
                P = np.array([[l.x, l.y, l.z]
                              for l in res.multi_hand_world_landmarks[0].landmark])
                feats = hand_features(P)
                smooth = feats if smooth is None else (
                    FEATURE_ALPHA * feats + (1 - FEATURE_ALPHA) * smooth)
                levels = quant.update(features_to_openness(smooth))
                angles = levels_to_angles(levels)
                sender.send(angles)
            # Mat tay: giu nguyen goc cuoi, khong gui lenh moi

            # ----- Giao dien toi gian -----
            color = (0, 255, 0) if has_hand else (160, 160, 160)
            status = "Co tay" if has_hand else "Khong thay tay"
            cv2.putText(frame, status, (10, 30), FONT, 0.8,
                        (0, 255, 0) if has_hand else (0, 0, 255), 2)
            for i in range(5):
                val = f"{angles[i]}" if angles else "--"
                cv2.putText(frame, f"{FINGER_NAMES[i]}: {val}", (10, 65 + i * 28),
                            FONT, 0.7, color, 2)

            cv2.imshow("Hand -> Servo", frame)
            key = cv2.waitKey(1) & 0xFF
            if key in (ord('q'), 27):
                break
            elif key == ord(' '):
                sender.enabled = not sender.enabled
            elif key == ord('o') and smooth is not None:
                OPEN_VAL = smooth.copy()
                save_calib()
                print("Da luu tay MO:", np.round(OPEN_VAL, 2))
            elif key == ord('f') and smooth is not None:
                CLOSED_VAL = smooth.copy()
                save_calib()
                print("Da luu tay NAM:", np.round(CLOSED_VAL, 2))

    cam.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()