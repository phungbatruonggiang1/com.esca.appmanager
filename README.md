# ESCA Application Manager

Who is installed, who is running, and who may start or stop them — one service
that answers all three, over D-Bus.

It is webOS's SAM rearranged for this system: the same split between a
catalogue, a running list and per-type life handlers, with Luna bus replaced by
D-Bus and the webOS-only parts left out.

## What it does

| | |
| --- | --- |
| **Catalogue** | Reads `appinfo.json` from `/usr/share/esca/applications` and `/home/weston/data/applications`. `ListApps`, `GetAppInfo`, `Reload` |
| **Lifecycle** | `Launch`, `Close`, `CloseByAppId`, `Pause`, `Resume`, `Running` |
| **Notification** | `AppLifeStatusChanged` fires for every transition, including an application that exits by itself |
| **Process hygiene** | Each app gets its own session; a reaper thread collects exits so nothing is left a zombie and a crash is reported rather than missed |

## What it deliberately does not do

- **No launch points.** In webOS they are the launcher's model of shortcuts.
  Our launcher is the Flutter dashboard, and it can read the catalogue.
- **No foreground tracking.** SAM learns this from LSM. Weston tells nobody
  which client is on top, so `getForegroundAppInfo` would be a guess.
- **No install or remove.** SAM delegates to AppInstallService; here the
  package manager owns that, and `Reload` picks up the result.
- **Only native applications.** `type` is checked, and a web or QML app is
  refused rather than silently not started, because there is no runtime to
  hand it to yet. That is the seam `ILifeHandler` exists for.

## appinfo.json

```json
{
  "id": "vn.sparc.esca.home",
  "title": "ESCA Home",
  "type": "native",
  "main": "/home/weston/data/esca-home/esca_home",
  "user": "weston",
  "env": { "GDK_BACKEND": "wayland" }
}
```

`main` may be relative, and is then resolved against the application's own
directory. `user` makes the manager drop privileges before exec — the
dashboard must run as `weston` to reach the compositor's socket.

The launcher sets `XDG_RUNTIME_DIR` and `WAYLAND_DISPLAY` itself, finding
whichever `wayland-*` socket Weston actually created rather than assuming
`wayland-0`. That assumption cost an afternoon when the dashboard was first
deployed by hand.

## D-Bus interface

`vn.sparc.esca.AppManager1` on the system bus, object
`/vn/sparc/esca/AppManager1`.

    ListApps()                        -> as
    GetAppInfo(s app_id)              -> a{sv}
    Launch(s app_id, a{ss} params)    -> s        instance id, empty on failure
    Close(s instance_id, b force)     -> i
    CloseByAppId(s app_id, b force)   -> i
    Pause(s instance_id)              -> i
    Resume(s instance_id)             -> i
    Running()                         -> a(ssisu) app, instance, pid, status, since
    Reload()                          -> i        applications found
    GetErrorMessage()                 -> s

Signals: `AppLifeStatusChanged(s app_id, s instance_id, s status)`,
`CatalogChanged()`.

Status moves `running` → `closing` → `stopped`, with `paused` in between when
asked. There is no `launching`: without a compositor to tell us when a window
appears, any such state would be invented.

## Trying it

    esca-appctl list
    esca-appctl launch vn.sparc.esca.home
    esca-appctl running
    esca-appctl watch &            # life-status changes as they happen
    esca-appctl close vn.sparc.esca.home-1

`ESCA_APP_DIRS=/tmp/apps esca-appmanagerd` points the catalogue somewhere else,
which is how to test without installing anything.

## Decisions worth knowing about

**Signals come from the reaper thread.** When an application exits on its own,
the thread that reaps it emits `AppLifeStatusChanged` directly. sdbus-c++
connections are safe to send from another thread, so this avoids a queue back
onto the event loop.

**Closing signals the process group, not the process.** An app that spawned
helpers takes them with it; `setsid()` at launch is what makes that safe.

**`Close` reports `closing`, not `stopped`.** The process is alive until the
reaper says otherwise. Claiming otherwise would let a launcher start a second
copy while the first still holds the Wayland surface.
