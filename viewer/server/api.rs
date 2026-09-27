//! The JSON API the browser talks to.

use std::sync::Arc;

use axum::Json;
use axum::Router;
use axum::extract::{Path, Query, State};
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use fabric::{Catalog, Evaluation, Site, Tile};
use serde::{Deserialize, Serialize};

use crate::state::{AppState, Family, LoadedPart};

/// Any failure a handler can report, rendered as JSON so the browser shows
/// the database's own words rather than a bare status code.
pub struct ApiError {
    status: StatusCode,
    message: String,
}

impl ApiError {
    fn not_found(message: impl Into<String>) -> Self {
        ApiError {
            status: StatusCode::NOT_FOUND,
            message: message.into(),
        }
    }

    fn bad_request(message: impl Into<String>) -> Self {
        ApiError {
            status: StatusCode::BAD_REQUEST,
            message: message.into(),
        }
    }

    fn internal(message: impl Into<String>) -> Self {
        ApiError {
            status: StatusCode::INTERNAL_SERVER_ERROR,
            message: message.into(),
        }
    }
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        #[derive(Serialize)]
        struct Body {
            error: String,
        }
        (
            self.status,
            Json(Body {
                error: self.message,
            }),
        )
            .into_response()
    }
}

type ApiResult<T> = Result<T, ApiError>;

/// Opens the part on a blocking thread: reading a grid is seconds of file
/// parsing and must not sit on an async worker.
async fn load(state: &Arc<AppState>, family: String, part: String) -> ApiResult<Arc<LoadedPart>> {
    let state = Arc::clone(state);
    tokio::task::spawn_blocking(move || state.load(&family, &part))
        .await
        .map_err(|error| ApiError::internal(format!("the load task failed: {error}")))?
        .map_err(ApiError::not_found)
}

#[derive(Serialize)]
struct FamiliesResponse {
    families: Vec<Family>,
}

async fn families(State(state): State<Arc<AppState>>) -> Json<FamiliesResponse> {
    Json(FamiliesResponse {
        families: state.families().to_vec(),
    })
}

/// The fabric, laid out as parallel arrays.
///
/// One object per tile would be some megabytes of braces for a part with
/// eighteen thousand tiles; this form uploads into GPU buffers as it stands.
#[derive(Serialize)]
struct GridResponse {
    family: String,
    part: String,
    width: u32,
    height: u32,
    /// Distinct tile types, indexed by `tile_type`.
    tile_types: Vec<String>,
    /// One entry per tile, in the snapshot's order.
    names: Vec<String>,
    x: Vec<u32>,
    y: Vec<u32>,
    tile_type: Vec<u32>,
    /// 1 when the tile has configuration bits of its own, so the renderer
    /// can tell the fabric apart from the interconnect filler.
    configurable: Vec<u8>,
}

async fn grid(
    State(state): State<Arc<AppState>>,
    Path((family, part)): Path<(String, String)>,
) -> ApiResult<Json<GridResponse>> {
    let loaded = load(&state, family, part).await?;
    let snapshot = &loaded.snapshot;

    let mut tile_types: Vec<String> = Vec::new();
    let mut type_index = std::collections::HashMap::new();
    let count = snapshot.tiles.len();
    let mut response = GridResponse {
        family: loaded.family.clone(),
        part: loaded.part.clone(),
        width: snapshot.width,
        height: snapshot.height,
        tile_types: Vec::new(),
        names: Vec::with_capacity(count),
        x: Vec::with_capacity(count),
        y: Vec::with_capacity(count),
        tile_type: Vec::with_capacity(count),
        configurable: Vec::with_capacity(count),
    };
    for tile in &snapshot.tiles {
        let next = tile_types.len() as u32;
        let index = *type_index.entry(tile.tile_type.clone()).or_insert_with(|| {
            tile_types.push(tile.tile_type.clone());
            next
        });
        response.names.push(tile.name.clone());
        response.x.push(tile.grid_x);
        response.y.push(tile.grid_y);
        response.tile_type.push(index);
        response
            .configurable
            .push(u8::from(!tile.bits_blocks.is_empty()));
    }
    response.tile_types = tile_types;
    Ok(Json(response))
}

#[derive(Serialize)]
struct TileResponse {
    tile: Tile,
    index: u32,
    sites: Vec<Site>,
}

async fn tile(
    State(state): State<Arc<AppState>>,
    Path((family, part, name)): Path<(String, String, String)>,
) -> ApiResult<Json<TileResponse>> {
    let loaded = load(&state, family, part).await?;
    let snapshot = &loaded.snapshot;
    let index = snapshot
        .find(&name)
        .ok_or_else(|| ApiError::not_found(format!("no tile named \"{name}\"")))?;
    let tile = snapshot.tiles[index].clone();
    let first = tile.site_first as usize;
    let last = (first + tile.site_count as usize).min(snapshot.sites.len());
    Ok(Json(TileResponse {
        sites: snapshot.sites[first..last].to_vec(),
        tile,
        index: index as u32,
    }))
}

#[derive(Deserialize)]
struct CatalogQuery {
    /// Case-insensitive substring the feature name must contain.
    #[serde(default)]
    filter: Option<String>,
    /// How many entries to return; the interconnect tile types document tens
    /// of thousands and no one reads them all at once.
    #[serde(default = "default_limit")]
    limit: usize,
}

fn default_limit() -> usize {
    500
}

#[derive(Serialize)]
struct CatalogResponse {
    #[serde(flatten)]
    catalog: Catalog,
    /// How many entries matched before the limit was applied.
    total: usize,
}

async fn catalog(
    State(state): State<Arc<AppState>>,
    Path((family, part, tile_type)): Path<(String, String, String)>,
    Query(query): Query<CatalogQuery>,
) -> ApiResult<Json<CatalogResponse>> {
    let loaded = load(&state, family, part).await?;
    let mut catalog = loaded
        .database
        .catalog(&tile_type)
        .map_err(|error| ApiError::not_found(error.to_string()))?;

    if let Some(filter) = query.filter.as_deref().filter(|text| !text.is_empty()) {
        let needle = filter.to_ascii_uppercase();
        catalog
            .entries
            .retain(|entry| entry.name.to_ascii_uppercase().contains(&needle));
        catalog
            .pseudo_pips
            .retain(|pip| pip.name.to_ascii_uppercase().contains(&needle));
    }
    let total = catalog.entries.len();
    catalog.entries.truncate(query.limit);
    catalog.pseudo_pips.truncate(query.limit);
    Ok(Json(CatalogResponse { catalog, total }))
}

#[derive(Deserialize)]
struct EvalRequest {
    text: String,
}

#[derive(Serialize)]
struct EvalResponse {
    #[serde(flatten)]
    evaluation: Evaluation,
    /// The tiles the evaluated features landed on, so the renderer can
    /// highlight them without a round trip per feature.
    tiles: Vec<Tile>,
}

async fn eval(
    State(state): State<Arc<AppState>>,
    Path((family, part)): Path<(String, String)>,
    Json(request): Json<EvalRequest>,
) -> ApiResult<Json<EvalResponse>> {
    // A pasted FASM file is fine; a multi-megabyte upload is not.
    const MAX_INPUT: usize = 1 << 20;
    if request.text.len() > MAX_INPUT {
        return Err(ApiError::bad_request(format!(
            "the input is {} bytes, more than the {MAX_INPUT} this accepts",
            request.text.len()
        )));
    }
    let loaded = load(&state, family, part).await?;
    let evaluation = tokio::task::spawn_blocking({
        let loaded = Arc::clone(&loaded);
        move || loaded.database.eval(&request.text)
    })
    .await
    .map_err(|error| ApiError::internal(format!("the evaluation task failed: {error}")))?
    .map_err(|error| ApiError::bad_request(error.to_string()))?;

    let mut tiles = Vec::new();
    let mut seen = std::collections::HashSet::new();
    for feature in &evaluation.features {
        if let Some(index) = feature.tile_index
            && seen.insert(index)
            && let Some(tile) = loaded.snapshot.tiles.get(index as usize)
        {
            tiles.push(tile.clone());
        }
    }
    Ok(Json(EvalResponse { evaluation, tiles }))
}

pub fn router(state: Arc<AppState>) -> Router {
    Router::new()
        .route("/api/families", get(families))
        .route("/api/parts/{family}/{part}/grid", get(grid))
        .route("/api/parts/{family}/{part}/tiles/{name}", get(tile))
        .route(
            "/api/parts/{family}/{part}/tile-types/{tile_type}",
            get(catalog),
        )
        .route("/api/parts/{family}/{part}/eval", post(eval))
        .with_state(state)
}
