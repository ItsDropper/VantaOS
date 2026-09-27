import json
import socket
import subprocess
import sys
import time

QMP_HOST = "127.0.0.1"
QMP_PORT = 5556


def get_clipboard():
    result = subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-Command",
            "Get-Clipboard -Raw"
        ],
        capture_output=True,
        text=True
    )

    return result.stdout


def send_qmp(sock, command):
    sock.sendall((json.dumps(command) + "\r\n").encode())


def wait_message(sock):
    data = b""

    while b"\r\n" not in data:
        chunk = sock.recv(4096)

        if not chunk:
            raise RuntimeError("QEMU closed the QMP connection.")

        data += chunk

    return json.loads(data.split(b"\r\n", 1)[0])


def key_event(sock, key, down):
    send_qmp(
        sock,
        {
            "execute": "input-send-event",
            "arguments": {
                "events": [
                    {
                        "type": "key",
                        "data": {
                            "down": down,
                            "key": {
                                "type": "qcode",
                                "data": key
                            }
                        }
                    }
                ]
            }
        }
    )


KEYS = {
    "a": "a",
    "b": "b",
    "c": "c",
    "d": "d",
    "e": "e",
    "f": "f",
    "g": "g",
    "h": "h",
    "i": "i",
    "j": "j",
    "k": "k",
    "l": "l",
    "m": "m",
    "n": "n",
    "o": "o",
    "p": "p",
    "q": "q",
    "r": "r",
    "s": "s",
    "t": "t",
    "u": "u",
    "v": "v",
    "w": "w",
    "x": "x",
    "y": "y",
    "z": "z",

    "0": "0",
    "1": "1",
    "2": "2",
    "3": "3",
    "4": "4",
    "5": "5",
    "6": "6",
    "7": "7",
    "8": "8",
    "9": "9",

    " ": "spc",
    "-": "minus",
    "=": "equal",
    "[": "bracket_left",
    "]": "bracket_right",
    "\\": "backslash",
    ";": "semicolon",
    "'": "apostrophe",
    ",": "comma",
    ".": "dot",
    "/": "slash",
    "`": "grave_accent",

    "\n": "ret",
    "\t": "tab",
}


SHIFTED = {
    "!": "1",
    "@": "2",
    "#": "3",
    "$": "4",
    "%": "5",
    "^": "6",
    "&": "7",
    "*": "8",
    "(": "9",
    ")": "0",

    "_": "minus",
    "+": "equal",

    "{": "bracket_left",
    "}": "bracket_right",

    "|": "backslash",
    ":": "semicolon",
    '"': "apostrophe",

    "<": "comma",
    ">": "dot",
    "?": "slash",

    "~": "grave_accent",
}


def send_character(sock, character):
    if character in SHIFTED:
        key = SHIFTED[character]

        key_event(sock, "shift", True)
        key_event(sock, key, True)
        key_event(sock, key, False)
        key_event(sock, "shift", False)

        return

    if character.isupper():
        key = character.lower()

        key_event(sock, "shift", True)
        key_event(sock, key, True)
        key_event(sock, key, False)
        key_event(sock, "shift", False)

        return

    key = KEYS.get(character)

    if key is None:
        return

    key_event(sock, key, True)
    key_event(sock, key, False)


def main():
    text = get_clipboard()

    if not text:
        print("Clipboard is empty.")
        return

    print("Clipboard:")
    print(repr(text))

    print("Connecting to QEMU...")

    sock = socket.create_connection(
        (QMP_HOST, QMP_PORT),
        timeout=3
    )

    try:
        greeting = wait_message(sock)

        send_qmp(
            sock,
            {
                "execute": "qmp_capabilities"
            }
        )

        wait_message(sock)

        for character in text:
            send_character(sock, character)
            time.sleep(0.02)

    finally:
        sock.close()

    print("Paste complete.")


if __name__ == "__main__":
    main()