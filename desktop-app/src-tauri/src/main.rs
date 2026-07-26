// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::process::{Command, Stdio};
use tauri::api::path::resource_dir;

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
    if let Some(resource) = resource_dir(app.config()) {
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
    let status = cli_command(&app)
        .args(["--daemon", "--host", "127.0.0.1", "--port", "6930"])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .map_err(|error| format!("SnapLLM CLI is unavailable: {error}"))?;
    if !status.success() {
        return Err(format!("SnapLLM daemon start failed ({status})"));
    }
    Ok("SnapLLM daemon start requested".to_string())
}

#[tauri::command]
fn daemon_stop(app: tauri::AppHandle) -> Result<String, String> {
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
        .invoke_handler(tauri::generate_handler![
            daemon_status,
            daemon_start,
            daemon_stop
        ])
        .run(tauri::generate_context!())
        .expect("error while running SnapLLM Desktop");
}
