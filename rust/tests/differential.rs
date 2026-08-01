//! Nightly differential conformance against a randomized C-reference corpus.

use std::{env, fs};

fn take<'a>(corpus: &'a [u8], offset: &mut usize, len: usize) -> &'a [u8] {
    let end = offset
        .checked_add(len)
        .filter(|&end| end <= corpus.len())
        .expect("truncated differential corpus");
    let value = &corpus[*offset..end];
    *offset = end;
    value
}

fn read_u32(corpus: &[u8], offset: &mut usize) -> u32 {
    u32::from_le_bytes(take(corpus, offset, 4).try_into().unwrap())
}

fn read_u64(corpus: &[u8], offset: &mut usize) -> u64 {
    u64::from_le_bytes(take(corpus, offset, 8).try_into().unwrap())
}

#[test]
fn randomized_c_reference_corpus() {
    let Ok(path) = env::var("HAYAHASH_CORPUS") else {
        eprintln!("HAYAHASH_CORPUS is unset; skipping nightly differential corpus");
        return;
    };
    let corpus = fs::read(&path).unwrap_or_else(|err| panic!("cannot read {path}: {err}"));
    let mut offset = 0;
    assert_eq!(take(&corpus, &mut offset, 8), b"HAYAFZ01");
    let case_count = read_u32(&corpus, &mut offset);
    let prng_seed = read_u64(&corpus, &mut offset);

    for case_index in 0..case_count {
        let len = read_u32(&corpus, &mut offset) as usize;
        let hash_seed = read_u64(&corpus, &mut offset);
        let expected = read_u64(&corpus, &mut offset);
        let input = take(&corpus, &mut offset, len);
        let actual = hayahash::hayahash64(input, hash_seed);
        assert_eq!(
            actual, expected,
            "case={case_index} len={len} hash_seed={hash_seed:#018x} \
             corpus_prng_seed={prng_seed:#018x}"
        );
    }

    assert_eq!(
        offset,
        corpus.len(),
        "trailing bytes in differential corpus"
    );
    eprintln!(
        "Rust matched {case_count} C-reference cases \
         (corpus PRNG seed={prng_seed:#018x})"
    );
}
