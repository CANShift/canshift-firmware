pub const WORKSHOP_TUNER_PATH_CAP: usize = 32;

const ROOT: &[u8] = b"/tuner/";
const INDEX: &[u8] = b"index.html";
const GZIP_SUFFIX: &[u8] = b".gz";
const QUERY: u8 = b'?';
const FRAGMENT: u8 = b'#';
const SEPARATOR: u8 = b'/';
const EXTENSION: u8 = b'.';
const FIRST_SAFE: u8 = b'!';
const LAST_SAFE: u8 = b'~';
const FORBIDDEN: &[u8] = b"\\%";
pub const DEFAULT_CONTENT_TYPE: &[u8] = b"application/octet-stream\0";

const CONTENT_TYPES: &[(&[u8], &[u8])] = &[
    (b"html", b"text/html; charset=utf-8\0"),
    (b"js", b"text/javascript; charset=utf-8\0"),
    (b"mjs", b"text/javascript; charset=utf-8\0"),
    (b"css", b"text/css; charset=utf-8\0"),
    (b"json", b"application/json\0"),
    (b"webmanifest", b"application/manifest+json\0"),
    (b"svg", b"image/svg+xml\0"),
    (b"png", b"image/png\0"),
    (b"ico", b"image/x-icon\0"),
    (b"woff2", b"font/woff2\0"),
    (b"txt", b"text/plain; charset=utf-8\0"),
];

fn path_part(uri: &[u8]) -> &[u8] {
    let end = uri
        .iter()
        .position(|&b| b == QUERY || b == FRAGMENT)
        .unwrap_or(uri.len());
    &uri[..end]
}

fn relative(uri: &[u8]) -> &[u8] {
    let path = path_part(uri);
    let trimmed = path.strip_prefix(&[SEPARATOR]).unwrap_or(path);
    if trimmed.is_empty() {
        INDEX
    } else {
        trimmed
    }
}

fn segment_safe(segment: &[u8]) -> bool {
    !segment.is_empty() && segment[0] != EXTENSION
}

fn relative_safe(rel: &[u8]) -> bool {
    let bytes_ok = rel
        .iter()
        .all(|&b| (FIRST_SAFE..=LAST_SAFE).contains(&b) && !FORBIDDEN.contains(&b));
    bytes_ok && rel.split(|&b| b == SEPARATOR).all(segment_safe)
}

#[must_use]
pub fn tuner_path(uri: &[u8], gzip: bool, out: &mut [u8]) -> Option<usize> {
    let rel = relative(uri);
    if !relative_safe(rel) {
        return None;
    }
    let suffix: &[u8] = if gzip {
        GZIP_SUFFIX
    } else {
        b""
    };
    let len = ROOT.len() + rel.len() + suffix.len();
    if len >= out.len().min(WORKSHOP_TUNER_PATH_CAP) {
        return None;
    }
    out[..ROOT.len()].copy_from_slice(ROOT);
    out[ROOT.len()..ROOT.len() + rel.len()].copy_from_slice(rel);
    out[ROOT.len() + rel.len()..len].copy_from_slice(suffix);
    out[len] = 0;
    Some(len)
}

fn last_segment(rel: &[u8]) -> &[u8] {
    rel.rsplit(|&b| b == SEPARATOR).next().unwrap_or(rel)
}

#[must_use]
pub fn spa_route(uri: &[u8]) -> bool {
    !last_segment(path_part(uri)).contains(&EXTENSION)
}

#[must_use]
pub fn content_type(uri: &[u8]) -> &'static [u8] {
    let segment = last_segment(relative(uri));
    let Some(dot) = segment.iter().rposition(|&b| b == EXTENSION) else {
        return DEFAULT_CONTENT_TYPE;
    };
    let extension = &segment[dot + 1..];
    CONTENT_TYPES
        .iter()
        .find(|(known, _)| known.eq_ignore_ascii_case(extension))
        .map_or(DEFAULT_CONTENT_TYPE, |&(_, mime)| mime)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn path(uri: &[u8], gzip: bool) -> Option<Vec<u8>> {
        let mut out = [0u8; WORKSHOP_TUNER_PATH_CAP];
        let len = tuner_path(uri, gzip, &mut out)?;
        Some(out[..len].to_vec())
    }

    #[test]
    fn the_root_serves_the_index() {
        assert_eq!(path(b"/", true).unwrap(), b"/tuner/index.html.gz");
        assert_eq!(path(b"", false).unwrap(), b"/tuner/index.html");
    }

    #[test]
    fn assets_map_under_the_studio_root() {
        assert_eq!(path(b"/a/app.js", true).unwrap(), b"/tuner/a/app.js.gz");
        assert_eq!(path(b"/app.css", false).unwrap(), b"/tuner/app.css");
    }

    #[test]
    fn query_and_fragment_are_ignored() {
        assert_eq!(path(b"/app.js?v=2", false).unwrap(), b"/tuner/app.js");
        assert_eq!(path(b"/?x#y", false).unwrap(), b"/tuner/index.html");
    }

    #[test]
    fn traversal_and_hidden_segments_are_refused() {
        assert!(path(b"/../config/device.json", false).is_none());
        assert!(path(b"/a/../b.js", false).is_none());
        assert!(path(b"/.env", false).is_none());
        assert!(path(b"/a//b.js", false).is_none());
    }

    #[test]
    fn encoded_and_odd_bytes_are_refused() {
        assert!(path(b"/%2e%2e/x", false).is_none());
        assert!(path(b"/a\\b.js", false).is_none());
        assert!(path(b"/a b.js", false).is_none());
        assert!(path(b"/caf\xc3\xa9.js", false).is_none());
    }

    #[test]
    fn names_past_the_spiffs_limit_are_refused() {
        assert_eq!(path(b"/abcdefghijklmnopqrstu", true).unwrap().len(), 31);
        assert!(path(b"/abcdefghijklmnopqrstuv", true).is_none());
        assert!(path(b"/abcdefghijklmnopqrstuv", false).is_some());
    }

    #[test]
    fn a_short_buffer_fails() {
        let mut out = [0u8; 8];
        assert!(tuner_path(b"/", false, &mut out).is_none());
    }

    #[test]
    fn extensionless_paths_are_spa_routes() {
        assert!(spa_route(b"/"));
        assert!(spa_route(b"/vehicle"));
        assert!(spa_route(b"/dash/page-2?tab=x"));
        assert!(!spa_route(b"/app.js"));
        assert!(!spa_route(b"/missing.png"));
    }

    #[test]
    fn content_type_follows_the_extension() {
        assert_eq!(content_type(b"/"), b"text/html; charset=utf-8\0");
        assert_eq!(
            content_type(b"/app.JS?x"),
            b"text/javascript; charset=utf-8\0"
        );
        assert_eq!(content_type(b"/f.woff2"), b"font/woff2\0");
        assert_eq!(content_type(b"/blob.bin"), DEFAULT_CONTENT_TYPE);
        assert_eq!(content_type(b"/vehicle"), DEFAULT_CONTENT_TYPE);
    }

    #[test]
    fn every_content_type_is_nul_terminated() {
        for (_, mime) in CONTENT_TYPES {
            assert_eq!(mime.last(), Some(&0));
        }
    }
}
