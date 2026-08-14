// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::net::{SocketAddr, TcpStream};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::OnceLock;
use std::time::Duration;
use tauri::Manager;

const DAEMON_ADDR: SocketAddr =
    SocketAddr::new(std::net::IpAddr::V4(std::net::Ipv4Addr::LOCALHOST), 6930);
const SUPERVISOR_POLL: Duration = Duration::from_secs(2);
const SUPERVISOR_FAILURE_LIMIT: u8 = 3;
const SUPERVISOR_RESTART_LIMIT: u8 = 5;
const SUPERVISOR_STABLE_TICKS: u8 = 30;

static SUPERVISOR_RUNNING: OnceLock<AtomicBool> = OnceLock::new();
static SUPERVISOR_STOP: AtomicBool = AtomicBool::new(false);

fn supervisor_running() -> &'static AtomicBool {
    SUPERVISOR_RUNNING.get_or_init(|| AtomicBool::new(false))
}

fn daemon_port_is_open() -> bool {
    TcpStream::connect_timeout(&DAEMON_ADDR, Duration::from_millis(500)).is_ok()
}

fn daemon_executable(app: &tauri::AppHandle) -> Option<std::path::PathBuf> {
    if let Ok(path) = std::env::var("SNAPLLM_CLI_PATH") {
        let candidate = std::path::PathBuf::from(path);
        if candidate.is_file() {
            return Some(candidate);
        }
    }
    let binary = if cfg!(windows) {
        "snapllm.exe"
    } else {
        "snapllm"
    };
    let mut candidates = Vec::new();
    if let Ok(current) = std::env::current_exe() {
        if let Some(parent) = current.parent() {
            candidates.push(parent.join(binary));
            candidates.push(parent.join("resources").join(binary));
        }
    }
    if let Ok(resource) = app.path().resource_dir() {
        candidates.push(resource.join(binary));
    }
    candidates.into_iter().find(|candidate| candidate.is_file())
}

fn cli_command(app: &tauri::AppHandle) -> Command {
    if let Some(path) = daemon_executable(app) {
        return Command::new(path);
    }
    Command::new(if cfg!(windows) {
        "snapllm.exe"
    } else {
        "snapllm"
    })
}

#[tauri::command]
fn daemon_status(app: tauri::AppHandle) -> Result<String, String> {
    let output = cli_command(&app)
        .arg("--daemon-status")
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .output()
        .map_err(|error| format!("SnapLLM CLI is unavailable: {error}"))?;
    if !output.status.success() {
        return Err(String::from_utf8_lossy(&output.stderr).trim().to_string());
    }
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

#[tauri::command]
fn daemon_start(app: tauri::AppHandle) -> Result<String, String> {
    // Attach the supervisor even when the daemon was started by the CLI or a
    // login task before Desktop opened. Do not ask the CLI to start a second
    // instance in that case.
    if daemon_port_is_open() {
        start_daemon_supervisor(app);
        return Ok("SnapLLM daemon already running; supervisor attached".to_string());
    }
    let status = cli_command(&app)
        .args(["--daemon", "--host", "127.0.0.1", "--port", "6930"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map_err(|error| format!("SnapLLM CLI is unavailable: {error}"))?;
    if !status.success() {
        return Err(format!("SnapLLM daemon start failed ({status})"));
    }
    start_daemon_supervisor(app);
    Ok("SnapLLM daemon start requested".to_string())
}

/// Keep the desktop-launched daemon available if it exits unexpectedly.
///
/// The browser build cannot own a local process, but the Tauri shell can. A
/// bounded supervisor closes the gap between a successful `--daemon` spawn and
/// a later child crash without creating an unbounded restart loop. Explicit
/// Stop sets `SUPERVISOR_STOP`, so user shutdown is never undone.
fn start_daemon_supervisor(app: tauri::AppHandle) {
    SUPERVISOR_STOP.store(false, Ordering::Release);
    if supervisor_running().swap(true, Ordering::AcqRel) {
        return;
    }
    std::thread::spawn(move || {
        let executable = daemon_executable(&app).unwrap_or_else(|| {
            std::path::PathBuf::from(if cfg!(windows) {
                "snapllm.exe"
            } else {
                "snapllm"
            })
        });
        let mut failures = 0u8;
        let mut restarts = 0u8;
        let mut stable_ticks = 0u8;
        loop {
            if SUPERVISOR_STOP.load(Ordering::Acquire) {
                break;
            }
            std::thread::sleep(SUPERVISOR_POLL);
            if daemon_port_is_open() {
                failures = 0;
                stable_ticks = stable_ticks.saturating_add(1);
                if stable_ticks >= SUPERVISOR_STABLE_TICKS {
                    // A stable minute earns a fresh bounded restart budget.
                    restarts = 0;
                    stable_ticks = 0;
                }
                continue;
            }
            stable_ticks = 0;
            failures = failures.saturating_add(1);
            if failures < SUPERVISOR_FAILURE_LIMIT || restarts >= SUPERVISOR_RESTART_LIMIT {
                continue;
            }
            failures = 0;
            let _ = Command::new(&executable)
                .args(["--daemon", "--host", "127.0.0.1", "--port", "6930"])
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status();
            restarts = restarts.saturating_add(1);
        }
        supervisor_running().store(false, Ordering::Release);
    });
}

#[tauri::command]
fn daemon_stop(app: tauri::AppHandle) -> Result<String, String> {
    SUPERVISOR_STOP.store(true, Ordering::Release);
    let status = cli_command(&app)
        .arg("--daemon-stop")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map_err(|error| format!("SnapLLM CLI is unavailable: {error}"))?;
    if !status.success() {
        return Err(format!("SnapLLM daemon stop failed ({status})"));
    }
    Ok("SnapLLM daemon stop requested".to_string())
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .invoke_handler(tauri::generate_handler![
            daemon_status,
            daemon_start,
            daemon_stop
        ])
        .run(tauri::generate_context!())
        .expect("error while running SnapLLM Desktop");
}
