// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::fs;
use std::path::PathBuf;

#[tauri::command]
fn scan_models_folder() -> Result<Vec<String>, String> {
    // Use the correct path (two levels up from src-tauri)
    let models_dir = PathBuf::from("../../models");

    if !models_dir.exists() {
        eprintln!("Models folder not found at: {:?}", models_dir);
        return Ok(Vec::new());
    }

    eprintln!("Scanning models folder at: {:?}", models_dir);
    scan_directory(&models_dir)
}

fn scan_directory(models_dir: &PathBuf) -> Result<Vec<String>, String> {
    let mut model_files = Vec::new();

    match fs::read_dir(&models_dir) {
        Ok(entries) => {
            for entry in entries {
                if let Ok(entry) = entry {
                    let path = entry.path();
                    if let Some(extension) = path.extension() {
                        if extension == "gguf" {
                            if let Some(file_name) = path.file_name() {
                                if let Some(name_str) = file_name.to_str() {
                                    let model_path = format!("models/{}", name_str);
                                    eprintln!("Found model: {}", model_path);
                                    model_files.push(model_path);
                                }
                            }
                        }
                    }
                }
            }
        }
        Err(e) => {
            eprintln!("Error reading directory: {}", e);
            return Err(format!("Failed to read models directory: {}", e));
        }
    }

    model_files.sort();
    eprintln!("Total models found: {}", model_files.len());
    Ok(model_files)
}

#[tauri::command]
async fn copy_model_to_folder(source_path: String) -> Result<String, String> {
    let source = PathBuf::from(&source_path);

    if !source.exists() {
        return Err("Source file does not exist".to_string());
    }

    if source.extension().and_then(|s| s.to_str()) != Some("gguf") {
        return Err("Only .gguf files are supported".to_string());
    }

    // Use the correct path (two levels up from src-tauri)
    let models_dir = PathBuf::from("../../models");
    if !models_dir.exists() {
        fs::create_dir_all(&models_dir)
            .map_err(|e| format!("Failed to create models directory: {}", e))?;
    }

    let file_name = source.file_name()
        .ok_or("Invalid file name")?;

    let destination = models_dir.join(file_name);

    if destination.exists() {
        return Err("A model with this name already exists in the models folder".to_string());
    }

    fs::copy(&source, &destination)
        .map_err(|e| format!("Failed to copy file: {}", e))?;

    // Return path relative to where the API server expects it
    let dest_filename = file_name.to_str()
        .ok_or("Failed to convert filename to string")?;

    Ok(format!("models/{}", dest_filename))
}

fn main() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            scan_models_folder,
            copy_model_to_folder
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
