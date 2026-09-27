//! The fabric viewer's server.
//!
//! It opens prjxray databases through the assembler's own facilities (see
//! `//fpga/ffi`), answers the browser's questions about the fabric, and
//! serves the wgpu front-end that draws the answers.

mod api;
mod state;

use std::net::SocketAddr;
use std::path::PathBuf;
use std::sync::Arc;

use clap::Parser;
use tower_http::services::{ServeDir, ServeFile};
use tower_http::trace::TraceLayer;

use crate::state::AppState;

#[derive(Parser, Debug)]
#[command(
    name = "fpga-fabric-viewer",
    about = "Serves a 2D viewer of the Xilinx fabrics the assembler supports."
)]
struct Args {
    /// Root of the prjxray database, the directory that holds the family
    /// directories (artix7, kintex7, ...).  Falls back to PRJXRAY_DB_PATH.
    #[arg(long, env = "PRJXRAY_DB_PATH")]
    prjxray_db_path: PathBuf,

    /// Directory holding index.html and the page's own assets.
    #[arg(long, default_value = "viewer/static")]
    static_dir: PathBuf,

    /// Directory holding the wasm bundle wasm-bindgen produced.  Kept apart
    /// from --static_dir because Bazel builds the two into different trees;
    /// both are served from the root of the site.
    #[arg(long, default_value = "viewer/web/bundle")]
    wasm_dir: PathBuf,

    /// Address to listen on.
    #[arg(long, default_value = "127.0.0.1:8080")]
    listen: SocketAddr,

    /// Open this part at startup instead of on the first request, so the
    /// first page load is not the one that waits for the parse.
    #[arg(long, value_name = "FAMILY/PART")]
    preload: Option<String>,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "info,tower_http=warn".into()),
        )
        .init();

    let args = Args::parse();
    if !args.prjxray_db_path.is_dir() {
        return Err(format!(
            "--prjxray_db_path {} is not a directory",
            args.prjxray_db_path.display()
        )
        .into());
    }
    let state = Arc::new(AppState::new(args.prjxray_db_path.clone())?);
    if state.families().is_empty() {
        return Err(format!(
            "no prjxray family directory under {}: expected one with mapping/parts.yaml",
            args.prjxray_db_path.display()
        )
        .into());
    }
    for family in state.families() {
        tracing::info!(
            family = %family.name,
            parts = family.parts.len(),
            "found a family"
        );
    }

    if let Some(preload) = args.preload.as_deref() {
        let (family, part) = preload
            .split_once('/')
            .ok_or_else(|| format!("--preload wants FAMILY/PART, got \"{preload}\""))?;
        let (family, part) = (family.to_string(), part.to_string());
        let state = Arc::clone(&state);
        tokio::task::spawn_blocking(move || {
            tracing::info!(%family, %part, "preloading");
            match state.load(&family, &part) {
                Ok(loaded) => tracing::info!(tiles = loaded.snapshot.tiles.len(), "preloaded"),
                Err(error) => tracing::error!(%error, "could not preload"),
            }
        })
        .await?;
    }

    let index = args.static_dir.join("index.html");
    if !index.is_file() {
        return Err(format!("{} does not exist", index.display()).into());
    }
    // Anything that is not the API is the front-end: the page's own files
    // first, then the wasm bundle, and an unknown path falls back to
    // index.html so a deep link still loads the page.
    let site = ServeDir::new(&args.static_dir)
        .fallback(ServeDir::new(&args.wasm_dir).fallback(ServeFile::new(index)));
    let app = api::router(Arc::clone(&state))
        .fallback_service(site)
        .layer(TraceLayer::new_for_http());

    let listener = tokio::net::TcpListener::bind(args.listen).await?;
    tracing::info!(address = %listener.local_addr()?, "listening");
    axum::serve(listener, app)
        .with_graceful_shutdown(async {
            let _ = tokio::signal::ctrl_c().await;
            tracing::info!("shutting down");
        })
        .await?;
    Ok(())
}
