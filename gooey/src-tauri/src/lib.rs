use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tauri::{AppHandle, Manager};
use tauri_plugin_shell::process::{CommandChild, CommandEvent};
use tauri_plugin_shell::ShellExt;

const FLASK_URL: &str = "http://127.0.0.1:5001";
const READY_TIMEOUT_SECS: u64 = 30;
const POLL_INTERVAL_MS: u64 = 200;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .setup(|app| {
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
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running Gooey");
}

fn start_sidecar(app: AppHandle, child_holder: Arc<Mutex<Option<CommandChild>>>) {
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

    // Poll until Flask responds on localhost:5001.
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
    if let Some(window) = app.get_webview_window("main") {
        let _ = window.navigate(FLASK_URL.parse().expect("invalid Flask URL"));
        std::thread::sleep(Duration::from_millis(300));
        let _ = window.show();
    }
}
