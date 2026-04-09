use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tauri::menu::{AboutMetadata, MenuBuilder, MenuItemBuilder, PredefinedMenuItem, SubmenuBuilder};
use tauri::{AppHandle, Manager};
use tauri_plugin_dialog::{DialogExt, MessageDialogButtons};
use tauri_plugin_shell::process::{CommandChild, CommandEvent};
use tauri_plugin_shell::ShellExt;
use tauri_plugin_updater::UpdaterExt;

const FLASK_URL: &str = "http://127.0.0.1:5254";
const DOCS_URL: &str  = "http://127.0.0.1:5254/docs/";
const READY_TIMEOUT_SECS: u64 = 30;
const POLL_INTERVAL_MS: u64 = 200;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            // ── Menu bar ──────────────────────────────────────────────────────
            let clear_cache    = MenuItemBuilder::with_id("clear_cache",    "Clear Cache").build(app)?;
            let dark_mode      = MenuItemBuilder::with_id("dark_mode",      "Toggle Dark Mode").build(app)?;
            let save_devices   = MenuItemBuilder::with_id("save_devices",   "Save Devices…").build(app)?;
            let load_devices   = MenuItemBuilder::with_id("load_devices",   "Load Devices…").build(app)?;
            let take_tour      = MenuItemBuilder::with_id("take_tour",      "Take Tour").build(app)?;
            let user_guide     = MenuItemBuilder::with_id("user_guide",     "User Guide").build(app)?;
            let check_updates  = MenuItemBuilder::with_id("check_updates",  "Check for Updates…").build(app)?;

            let app_submenu = SubmenuBuilder::new(app, "annieData")
                .item(&PredefinedMenuItem::about(app, None, Some(AboutMetadata::default()))?)
                .separator()
                .item(&dark_mode)
                .item(&clear_cache)
                .separator()
                .item(&PredefinedMenuItem::quit(app, None)?)
                .build()?;
            let file_submenu = SubmenuBuilder::new(app, "File")
                .item(&save_devices)
                .item(&load_devices)
                .build()?;
            let edit_submenu = SubmenuBuilder::new(app, "Edit")
                .undo()
                .redo()
                .separator()
                .cut()
                .copy()
                .paste()
                .select_all()
                .build()?;
            let help_submenu = SubmenuBuilder::new(app, "Help")
                .item(&take_tour)
                .item(&user_guide)
                .separator()
                .item(&check_updates)
                .build()?;
            let menu = MenuBuilder::new(app)
                .item(&app_submenu)
                .item(&file_submenu)
                .item(&edit_submenu)
                .item(&help_submenu)
                .build()?;
            app.set_menu(menu)?;
            app.on_menu_event(|app, event| {
                let win = match app.get_webview_window("main") {
                    Some(w) => w,
                    None => return,
                };
                match event.id().0.as_str() {
                    "clear_cache"   => { let _ = win.eval("localStorage.clear(); location.reload();"); }
                    "dark_mode"     => { let _ = win.eval("document.getElementById('btnThemeToggle').click();"); }
                    "save_devices"  => { let _ = win.eval("document.getElementById('btnSaveDevices').click();"); }
                    "load_devices"  => { let _ = win.eval("document.getElementById('deviceFileInput').click();"); }
                    "take_tour"     => { let _ = win.eval("if(window._gooeyTour) window._gooeyTour.start();"); }
                    "user_guide"    => { let _ = win.eval(&format!("window.open('{}','_blank');", DOCS_URL)); }
                    "check_updates" => {
                        let h = app.clone();
                        tauri::async_runtime::spawn(async move {
                            check_and_offer_update(h, true).await;
                        });
                    }
                    _ => {}
                }
            });
            // ─────────────────────────────────────────────────────────────────

            let child_holder: Arc<Mutex<Option<CommandChild>>> = Arc::new(Mutex::new(None));
            let child_for_event = child_holder.clone();

            // Register window close handler on the window itself (not AppHandle).
            if let Some(window) = app.get_webview_window("main") {
                window.on_window_event(move |event| {
                    if matches!(
                        event,
                        tauri::WindowEvent::CloseRequested { .. }
                            | tauri::WindowEvent::Destroyed
                    ) {
                        if let Ok(mut guard) = child_for_event.lock() {
                            if let Some(c) = guard.take() {
                                let _ = c.kill();
                            }
                        }
                    }
                });
            }

            let handle = app.handle().clone();
            std::thread::spawn(move || start_sidecar(handle, child_holder));

            // Background update check — silent if no update found.
            let update_handle = app.handle().clone();
            tauri::async_runtime::spawn(async move {
                check_and_offer_update(update_handle, false).await;
            });

            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running Gooey");
}

/// Check for an available update and offer to install it.
///
/// `show_if_current` — when true, shows a "You're up to date" dialog if no
/// update is found (used for the "Check for Updates…" menu item).  When false
/// (background startup check), nothing is shown if the app is already current.
async fn check_and_offer_update(app: AppHandle, show_if_current: bool) {
    let updater = match app.updater() {
        Ok(u) => u,
        Err(e) => {
            eprintln!("[updater] init failed: {e}");
            return;
        }
    };

    match updater.check().await {
        Ok(Some(update)) => {
            let version = update.version.clone();
            let notes   = update.body.clone().unwrap_or_default();

            // Wrap in Arc<Mutex<Option<…>>> so we can move it into the callback.
            let update = Arc::new(Mutex::new(Some(update)));
            let app2   = app.clone();

            app.dialog()
                .message(format!(
                    "annieData {version} is available.\n\n{notes}\n\nInstall now and restart?"
                ))
                .title("Update Available")
                .buttons(MessageDialogButtons::OkCancelCustom(
                    "Install & Restart".to_string(),
                    "Later".to_string(),
                ))
                .show(move |answer| {
                    if !answer { return; }
                    let update = update.clone();
                    let app    = app2.clone();
                    tauri::async_runtime::spawn(async move {
                        let u = update.lock().unwrap().take();
                        if let Some(u) = u {
                            if let Err(e) = u
                                .download_and_install(|_, _| {}, || {})
                                .await
                            {
                                eprintln!("[updater] install failed: {e}");
                            } else {
                                app.restart();
                            }
                        }
                    });
                });
        }
        Ok(None) => {
            if show_if_current {
                app.dialog()
                    .message("You're up to date — this is the latest version of annieData.")
                    .title("No Update Available")
                    .show(|_| {});
            }
        }
        Err(e) => {
            eprintln!("[updater] check failed: {e}");
            if show_if_current {
                app.dialog()
                    .message(format!("Could not check for updates:\n{e}"))
                    .title("Update Check Failed")
                    .show(|_| {});
            }
        }
    }
}

/// On macOS 12+, the OS silently blocks LAN UDP unless the app has been
/// granted local-network permission. Permission is only requested when the
/// *main app process* (which owns the Info.plist) makes a local-network
/// connection attempt — child processes / raw sidecars never trigger the
/// dialog on their own.
///
/// Sending a zero-byte datagram to the mDNS multicast address is the
/// canonical trigger: the send itself will fail, but macOS intercepts the
/// attempt and shows the "annieData wants to find and connect to devices on
/// your local network" prompt using our NSLocalNetworkUsageDescription.
#[cfg(target_os = "macos")]
fn request_local_network_permission() {
    use std::net::UdpSocket;
    if let Ok(sock) = UdpSocket::bind("0.0.0.0:0") {
        let _ = sock.send_to(&[], "224.0.0.251:5353");
    }
}

#[cfg(not(target_os = "macos"))]
fn request_local_network_permission() {}

fn start_sidecar(app: AppHandle, child_holder: Arc<Mutex<Option<CommandChild>>>) {
    // Trigger the macOS local-network permission dialog before the sidecar
    // starts sending OSC traffic (no-op on other platforms).
    request_local_network_permission();

    let (mut rx, child) = app
        .shell()
        .sidecar("gooey-server")
        .expect("gooey-server sidecar not found — run `npm run build:sidecar` first")
        .spawn()
        .expect("failed to spawn gooey-server");

    // Store child so the window event handler can kill it on close.
    if let Ok(mut guard) = child_holder.lock() {
        *guard = Some(child);
    }

    // Forward sidecar output to host terminal for debugging.
    std::thread::spawn(move || {
        while let Some(event) = rx.blocking_recv() {
            match event {
                CommandEvent::Stdout(line) => {
                    print!("[gooey-server] {}", String::from_utf8_lossy(&line));
                }
                CommandEvent::Stderr(line) => {
                    eprint!("[gooey-server] {}", String::from_utf8_lossy(&line));
                }
                CommandEvent::Terminated(status) => {
                    eprintln!("[gooey-server] terminated: {:?}", status);
                    break;
                }
                _ => {}
            }
        }
    });

    // Poll until Flask responds on localhost:5254.
    let deadline = Instant::now() + Duration::from_secs(READY_TIMEOUT_SECS);
    loop {
        if Instant::now() >= deadline {
            eprintln!("[gooey] server did not become ready within {READY_TIMEOUT_SECS}s");
            return;
        }
        if ureq::get(FLASK_URL).call().is_ok() {
            break;
        }
        std::thread::sleep(Duration::from_millis(POLL_INTERVAL_MS));
    }

    // Navigate main window to Flask and show it.
    // Use a unique timestamp query param so WebKit never serves a cached page
    // from a prior version — the URL is different every launch.
    if let Some(window) = app.get_webview_window("main") {
        let ts = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0);
        let url = format!("{}/?_r={}", FLASK_URL, ts);
        let _ = window.navigate(url.parse().expect("invalid Flask URL"));
        std::thread::sleep(Duration::from_millis(300));
        let _ = window.show();
    }
}
