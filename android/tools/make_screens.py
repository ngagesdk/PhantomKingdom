"""Renders the store screenshots: HTML copies of the Compose screens (same
texts, colours and layout), 360x720 dp at 3x = 1080x2160 px."""
import sys, pathlib, shutil, tempfile
from playwright.sync_api import sync_playwright

OUT = pathlib.Path(sys.argv[1]); OUT.mkdir(parents=True, exist_ok=True)
HERE = pathlib.Path(tempfile.mkdtemp())
shutil.copy(pathlib.Path(__file__).resolve().parent.parent / "app/src/main/res/drawable-nodpi/logo.png", HERE / "logo.png")

LIGHT = dict(bg="#FAF8FD", on="#1C1A1F", onv="#5D5866", card="#FFFFFF", outv="#E2DCE8", prim="#8A12D6", onprim="#FFFFFF",
             pc="#F1E3FF", sc="#E8FBB4", onsc="#162100", nav="#FFFFFF", outline="#8E8796", ok="#5E9900", busy="#E09A00",
             off="#B9B2C1", bad="#D32F2F", track="#EDE7F3")
DARK = dict(bg="#131016", on="#ECE6F0", onv="#CBC3D3", card="#1C1820", outv="#3A3540", prim="#D7A6FF", onprim="#3D0068",
            pc="#5A1194", sc="#3A4F00", onsc="#E8FBB4", nav="#0E0C10", outline="#958E9D", ok="#B9EE3A", busy="#FFC94A",
            off="#5D5866", bad="#FF8A80", track="#2B2630")

ICON = {
 "home": "M10 20v-6h4v6h5v-8h3L12 3 2 12h3v8z",
 "list": "M3 13h2v-2H3v2zm0 4h2v-2H3v2zm0-8h2V7H3v2zm4 4h14v-2H7v2zm0 4h14v-2H7v2zM7 7v2h14V7H7z",
 "settings": "M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58a.49.49 0 0 0 .12-.61l-1.92-3.32a.488.488 0 0 0-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54a.484.484 0 0 0-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58a.49.49 0 0 0-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z",
 "play": "M8 5v14l11-7z",
 "wifi": "M1 9l2 2c4.97-4.97 13.03-4.97 18 0l2-2C16.93 2.93 7.08 2.93 1 9zm8 8l3 3 3-3c-1.65-1.66-4.34-1.66-6 0zm-4-4l2 2c2.76-2.76 7.24-2.76 10 0l2-2C15.14 9.14 8.87 9.14 5 13z",
 "battery": "M15.67 4H14V2h-4v2H8.33C7.6 4 7 4.6 7 5.33v15.33C7 21.4 7.6 22 8.33 22h7.33c.74 0 1.34-.6 1.34-1.33V5.33C17 4.6 16.4 4 15.67 4z",
 "share": "M18 16.08c-.76 0-1.44.3-1.96.77L8.91 12.7c.05-.23.09-.46.09-.7s-.04-.47-.09-.7l7.05-4.11c.54.5 1.25.81 2.04.81 1.66 0 3-1.34 3-3s-1.34-3-3-3-3 1.34-3 3c0 .24.04.47.09.7L8.04 9.81C7.5 9.31 6.79 9 6 9c-1.66 0-3 1.34-3 3s1.34 3 3 3c.79 0 1.5-.31 2.04-.81l7.12 4.16c-.05.21-.08.43-.08.65 0 1.61 1.31 2.92 2.92 2.92s2.92-1.31 2.92-2.92-1.31-2.92-2.92-2.92z",
 "delete": "M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z",
}
def svg(name, size=24, color="currentColor"):
    return f'<svg width="{size}" height="{size}" viewBox="0 0 24 24"><path fill="{color}" d="{ICON[name]}"/></svg>'

CSS = """
*{box-sizing:border-box;margin:0;padding:0}
body{width:360px;height:720px;background:var(--bg);color:var(--on);font-family:Inter,Roboto,sans-serif;overflow:hidden;position:relative}
.status{height:28px;display:flex;align-items:center;justify-content:space-between;padding:0 18px;font-size:14px;font-weight:500}
.content{padding:0 20px}
.logo{display:flex;justify-content:center;padding:12px 0 4px}.logo img{height:60px}
.card{border-radius:24px;background:var(--card);border:1px solid var(--outv);padding:6px 0}
.play{border-radius:24px;padding:20px;display:flex;align-items:center;gap:16px;margin-top:12px}
.play h2{font-size:22px;font-weight:600;line-height:28px}.play p{font-size:14px;color:var(--onv);line-height:20px;margin-top:4px}
.fab{width:68px;height:68px;border-radius:50%;display:flex;align-items:center;justify-content:center;flex:none}
.title{font-size:14px;font-weight:600;color:var(--onv);padding:20px 4px 8px;display:flex;justify-content:space-between}
.row{display:flex;align-items:center;padding:12px 18px}
.dot{width:10px;height:10px;border-radius:50%;flex:none;margin-right:14px}
.lbl{font-size:12px;color:var(--onv);font-weight:500}.val{font-size:16px;line-height:22px}
.div{height:1px;background:var(--outv);margin:0 18px}
.tb{color:var(--prim);font-size:14px;font-weight:500;padding:10px 12px;white-space:nowrap}
.btns{display:flex;gap:10px}.tonal{flex:1;background:var(--sc);color:var(--onsc);border-radius:20px;height:40px;display:flex;align-items:center;justify-content:center;font-size:14px;font-weight:500}
.outl{border:1px solid var(--outline);color:var(--prim);border-radius:20px;height:40px;display:flex;align-items:center;justify-content:center;font-size:14px;font-weight:500;margin-top:4px}
.small{font-size:12px;color:var(--onv);line-height:16px}
.step{display:flex;font-size:14px;line-height:20px;margin-bottom:10px}.step b{color:var(--prim);width:22px;flex:none}
.nav{position:absolute;bottom:0;left:0;right:0;height:80px;background:var(--nav);display:flex;justify-content:space-around;padding-top:12px}
.ni{display:flex;flex-direction:column;align-items:center;gap:4px;font-size:12px;font-weight:500;color:var(--onv);width:90px}
.ind{width:64px;height:32px;border-radius:16px;display:flex;align-items:center;justify-content:center}
.ni.sel .ind{background:var(--pc)}.ni.sel{color:var(--on)}
.field{border:1px solid var(--outline);border-radius:4px;height:56px;padding:0 16px;display:flex;align-items:center;font-size:16px;position:relative;margin-top:8px}
.field span{position:absolute;top:-8px;left:12px;background:var(--card);padding:0 4px;font-size:12px;color:var(--onv)}
.sup{font-size:12px;color:var(--onv);padding:4px 16px 0;line-height:16px}
.seg{display:flex;border:1px solid var(--outline);border-radius:20px;overflow:hidden;height:40px}
.seg div{flex:1;display:flex;align-items:center;justify-content:center;font-size:14px;font-weight:500;border-left:1px solid var(--outline)}
.seg div:first-child{border-left:none}.seg .on{background:var(--sc);color:var(--onsc)}
.bar{height:4px;border-radius:2px;background:var(--track);margin:0 18px 10px;overflow:hidden}.bar div{height:100%;width:38%;margin-left:30%;background:var(--prim);border-radius:2px}
.log{font-family:'DejaVu Sans Mono',monospace;font-size:12px;line-height:16px;padding:3px 0;display:flex;gap:10px}.log span:last-child{word-break:break-all;min-width:0}
.err{background:#FFDAD6;color:#410002;border-radius:24px;padding:18px;font-size:14px;margin-top:12px}
"""

def page(theme, body, tab):
    vars_ = ";".join(f"--{k}:{v}" for k, v in theme.items())
    nav = "".join(
        f'<div class="ni{" sel" if i == tab else ""}"><div class="ind">{svg(ic)}</div>{lbl}</div>'
        for i, (lbl, ic) in enumerate([("Game", "home"), ("Log", "list"), ("Settings", "settings")]))
    status = f'<div class="status"><span>9:41</span><span style="display:flex;gap:4px">{svg("wifi",16)}{svg("battery",16)}</span></div>'
    return f'<html><head><style>:root{{{vars_}}}{CSS}</style></head><body>{status}<div class="content">' \
           f'<div class="logo"><img src="logo.png"></div>{body}</div><div class="nav">{nav}</div></body></html>'

def row(lbl, val, color, action=""):
    a = f'<div class="tb">{action}</div>' if action else ""
    return f'<div class="row"><div class="dot" style="background:var(--{color})"></div><div style="flex:1"><div class="lbl">{lbl}</div><div class="val">{val}</div></div>{a}</div>'

def play(ready):
    if ready:
        return f'<div class="play" style="background:var(--pc)"><div style="flex:1"><h2>Ready to play</h2><p>Your kingdom is waiting online.</p></div><div class="fab" style="background:var(--prim)">{svg("play",32,"var(--onprim)")}</div></div>'
    return f'<div class="play card" style="padding:20px"><div style="flex:1"><h2>Game data needed</h2><p>Import the game files below to start.</p></div><div class="fab" style="background:color-mix(in srgb, var(--on) 12%, transparent)">{svg("play",32,"color-mix(in srgb, var(--on) 38%, transparent)")}</div></div>'

NOTE = '<div class="small" style="margin-top:8px">Use the original game files from your own Pocket Kingdom game card. The app does not include or download any game data.</div>'

def home_ready():
    return play(True) + '<div class="title">Status</div><div class="card">' + \
        row("Game data", "1,248 files, 38.4 MB", "ok") + '<div class="div"></div>' + \
        row("Server", "188.68.43.183:80 reachable (42 ms)", "ok", "Test") + '<div class="div"></div>' + \
        row("N-Gage Arena account", "kingdomfan", "ok") + '</div>' + \
        '<div class="tb" style="padding:10px 12px">Server and account</div>' + \
        '<div class="title">Game data</div><div class="card"><div style="padding:10px 18px">' + \
        '<div style="font-size:16px;font-weight:500">PocketKingdom.zip</div><div class="small">Imported 2 minutes ago</div>' + \
        '<div class="btns" style="margin-top:12px"><div class="tonal">ZIP archive</div><div class="tonal">Folder</div></div>' + \
        '<div class="outl">Remove game data</div>' + NOTE + '</div></div>'

def home_start():
    return play(False) + '<div class="title">Status</div><div class="card">' + \
        row("Game data", "Not imported", "off") + '<div class="div"></div>' + \
        row("Server", "188.68.43.183", "off", "Test") + '<div class="div"></div>' + \
        row("N-Gage Arena account", "Not set, needed for online play", "off") + '</div>' + \
        '<div class="tb" style="padding:10px 12px">Server and account</div>' + \
        '<div class="title">Game data</div><div class="card"><div style="padding:10px 18px">' + \
        '<div class="step"><b>1</b>Copy the original game files from your Pocket Kingdom game card to this phone, as a folder or a ZIP archive.</div>' + \
        '<div class="step"><b>2</b>Tap ZIP archive or Folder and select them.</div><div class="step"><b>3</b>Tap the play button above.</div>' + \
        '<div class="btns" style="margin-top:4px"><div class="tonal">ZIP archive</div><div class="tonal">Folder</div></div>' + NOTE + '</div></div>'

def home_importing():
    return play(False) + '<div class="title">Status</div><div class="card">' + \
        row("Game data", "Importing… 612 files, 17.9 MB", "busy", "Cancel") + '<div class="bar"><div></div></div><div class="div"></div>' + \
        row("Server", "188.68.43.183:80 reachable (38 ms)", "ok", "Test") + '</div>'

def settings():
    return '<div class="title">Server</div><div class="card"><div style="padding:10px 18px">' + \
        '<div class="field"><span>Server address</span>188.68.43.183</div><div class="sup">PhantomLink server; empty uses 188.68.43.183</div>' + \
        '<div class="tb" style="color:color-mix(in srgb, var(--on) 38%, transparent);padding:10px 0 4px">Use the default server</div></div></div>' + \
        '<div class="title">N-Gage Arena account</div><div class="card"><div style="padding:10px 18px">' + \
        '<div class="field"><span>User name</span>kingdomfan</div>' + \
        '<div class="field" style="margin-top:14px"><span>Password</span>••••••••</div>' + \
        '<div class="small" style="margin-top:8px">Your account on the PhantomLink server, for N-Gage Arena in the game\'s title menu. Handed to the game when it starts; leave empty to keep what the game has stored.</div></div></div>' + \
        '<div class="title">Player</div><div class="card"><div style="padding:6px 18px 8px"><div class="lbl">N-Gage ID (IMEI)</div>' + \
        '<div class="val" style="font-family:\'DejaVu Sans Mono\',monospace;font-size:15px">IMEI</div>' + \
        '<div class="small" style="margin-top:4px">PhantomLink binds your Arena account to this number, like to an N-Gage\'s IMEI. It is random and stays the same until the app is uninstalled.</div></div></div>'

def log():
    lines = [("10:02:11", 3, "PhantomKingdom 0.1.0 started"), ("10:02:11", 2, "Server 188.68.43.183:80 reachable (42 ms)"),
             ("10:03:40", 2, "Importing game data from PocketKingdom.zip"), ("10:03:52", 2, "Imported 1248 files (40265318 bytes) from PocketKingdom.zip"),
             ("10:04:05", 2, "Game started: --data /data/user/0/de.ngagesdk.phantomkingdom/files/game/System/Apps/6RAH --arena-host 188.68.43.183 --arena-user kingdomfan --arena-pass ***")]
    col = {3: "var(--onv)", 2: "var(--on)", 1: "var(--busy)", 0: "var(--bad)"}
    body = f'<div style="display:flex;align-items:center;padding:8px 0 0 4px"><div style="flex:1;font-size:22px">Log</div>' \
           f'<div style="padding:12px;color:var(--onv)">{svg("share")}</div><div style="padding:12px;color:var(--onv)">{svg("delete")}</div></div>'
    body += "".join(f'<div class="log"><span style="color:var(--outline)">{t}</span><span style="color:{col[l]}">{x}</span></div>' for t, l, x in lines)
    return body

shots = [("1_ready_to_play", LIGHT, home_ready(), 0), ("2_getting_started", LIGHT, home_start(), 0),
         ("3_importing", LIGHT, home_importing(), 0), ("4_settings", LIGHT, settings(), 2),
         ("5_log", LIGHT, log(), 1), ("6_dark_mode", DARK, home_ready(), 0)]
with sync_playwright() as p:
    b = p.chromium.launch()
    pg = b.new_page(viewport={"width": 360, "height": 720}, device_scale_factor=3)
    for name, theme, body, tab in shots:
        f = HERE / f"{name}.html"
        f.write_text(page(theme, body, tab), encoding="utf-8")
        pg.goto(f.as_uri()); pg.wait_for_timeout(200)
        pg.screenshot(path=str(OUT / f"{name}.png"))
    b.close()
print("screens written")
