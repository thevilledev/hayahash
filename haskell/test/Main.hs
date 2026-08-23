{-# LANGUAGE BangPatterns #-}

module Main (main) where

import Control.Monad (forM_, unless)
import Data.Bits (shiftL, shiftR, (.|.))
import qualified Data.ByteString as BS
import qualified Data.ByteString.Char8 as BSC
import Data.Hash.Hayahash
import Data.List (foldl')
import Data.Word (Word32, Word64)
import Numeric (showHex)
import System.Environment (lookupEnv)

k :: Word64
k = 0x9E3779B97F4A7C15

patternA :: Int -> BS.ByteString
patternA n =
  BS.pack
    [ fromIntegral (((fromIntegral i * k) + 0x2545F4914F6CDD1D) `shiftR` 56)
      | i <- [0 .. n - 1]
    ]

hex64 :: Word64 -> String
hex64 value = "0x" ++ replicate (16 - length digits) '0' ++ digits
  where
    digits = showHex value ""

assertEqual :: (Eq a, Show a) => String -> a -> a -> IO ()
assertEqual label expected actual =
  unless (actual == expected) $
    ioError (userError (label ++ ": got " ++ show actual ++ ", want " ++ show expected))

assertWord64 :: String -> Word64 -> Word64 -> IO ()
assertWord64 label expected actual =
  unless (actual == expected) $
    ioError (userError (label ++ ": got " ++ hex64 actual ++ ", want " ++ hex64 expected))

-- C-reference vectors spanning every dispatch boundary. The full randomized
-- C corpus is consumed by differentialTest when HAYAHASH_CORPUS is set.
knownVectors :: [(Int, Word64, Word64, Word64)]
knownVectors =
  [ (0, 0, 0x68AC507CF298CA3F, 0xACE2141F6BA30868),
    (0, k, 0xC4F85F43D5A9985E, 0xF19B4366D85972A9),
    (0, 0xDEADBEEFCAFEBABE, 0x7EDC9F1B603B7337, 0xA712E25AF7D8FBE3),
    (1, 0, 0x7EC9660A48395D15, 0x3CF0E5F69813C72D),
    (3, 0, 0x10E8B8FEA2D42E52, 0xBA18588FBEED54B1),
    (4, 0, 0x3FF33333AEEA0226, 0xFB1361E3305035B6),
    (7, 0, 0x8D33EEB37AEA4269, 0x0B830933A564862D),
    (8, 0, 0xA7E6D3110DA23914, 0x85B472470E04F87E),
    (15, 0, 0x9A8920A57F119D6B, 0x812C68AF6D9A1980),
    (16, 0, 0xE1AF813939BA1A9E, 0x859C307ABAFCFE89),
    (17, 0, 0xEB0531E9E3A3BEBE, 0x8D3299B49DD5F0B4),
    (31, 0, 0x95D2421945AEC7A1, 0x7B27C96BCEA1D737),
    (32, 0, 0xCBD35DAB7AD91CE4, 0x46001BDFFAD28749),
    (33, 0, 0x134D1F8689BF729C, 0x65DA20A11680F011),
    (63, 0, 0x7FD21B276D3862D5, 0xAB06C32C061A550B),
    (64, 0, 0x8D2CE2017D1ECCEB, 0x068054E324D8E376),
    (65, 0, 0xA521C43309772CDE, 0x9DAF5FE41F93B62D),
    (127, 0, 0x4907F10A034954D1, 0x3F2C380E36ECDB6A),
    (128, 0, 0xEECEEE2B8790729D, 0xFB5938F230779DF7),
    (191, 0, 0xB9E354ABAF76CDA3, 0xE5C43AFEFF7529E0),
    (192, 0, 0x0503FD18DB80FFFF, 0xB82535A2F1CC8B0E),
    (255, 0, 0x1D0EE105FC8EE266, 0x3DCC2C2A5FCE8BA4),
    (319, 0, 0x8F078F3394AC0EEB, 0x3DF43DA730823B08),
    (320, 0, 0xF4BCF4FA135AABFE, 0x85E0275273C4BFB5),
    (321, 0, 0x68868A120FB9CEF6, 0x11611CDDC7687333),
    (383, 0, 0x762CF976C6FFBA80, 0xBB395DD49DC33393),
    (512, 0, 0xDFBF7FC9292FF7FF, 0xEA2AD390FF4B23AB),
    (1023, 0, 0x2578244C81138967, 0x643F2D2A075C1073),
    (1024, 0, 0x951BE6CF3BC7CF43, 0x45211E0641A2C10D),
    (320, k, 0x6F86504F4C61F014, 0xDD90FF3299ACC673),
    (1024, 0xDEADBEEFCAFEBABE, 0xC93D1F81FD51336A, 0xB47A82E46FF97BD4)
  ]

knownAnswerTest :: IO ()
knownAnswerTest =
  forM_ knownVectors $ \(len, seed, expectedLow, expectedHigh) -> do
    let input = patternA len
        actual = hayahash128 input seed
        label = "KAT len=" ++ show len ++ " seed=" ++ hex64 seed
    assertWord64 (label ++ " h64") expectedLow (hayahash64 input seed)
    assertWord64 (label ++ " h128.lo") expectedLow (hashLow64 actual)
    assertWord64 (label ++ " h128.hi") expectedHigh (hashHigh64 actual)

literalTest :: IO ()
literalTest = do
  let input = BSC.pack "hello world"
      actual = hayahash128 input 0
  assertWord64 "hello world h64" 0x4524B96611BFC05A (hayahash64 input 0)
  assertWord64 "hello world h128.hi" 0x41D8671459F6EEF8 (hashHigh64 actual)

word64LE :: Word64 -> BS.ByteString
word64LE word = BS.pack [fromIntegral (word `shiftR` (8 * i)) | i <- [0 .. 7]]

verificationTest :: IO ()
verificationTest = do
  let key = BS.pack [0 .. 255]
      hashes =
        BS.concat
          [ word64LE (hayahash64 (BS.take i key) (fromIntegral (256 - i)))
            | i <- [0 .. 255]
          ]
      actual = fromIntegral (hayahash64 hashes 0) :: Word32
  assertEqual "SMHasher3 verification value" 0x65F2AC15 actual

chunkSize :: Int -> Int -> Int -> Int
chunkSize patternIndex chunkIndex remaining =
  min remaining $
    case patternIndex of
      0 -> 1
      1 -> 7
      2 -> 64
      3 -> 127
      4 -> 448
      5 -> 449
      6 -> max 1 remaining
      _ -> 1 + ((chunkIndex * 31 + 7) `mod` 193)

feed :: Word64 -> Int -> BS.ByteString -> Hasher
feed seed patternIndex input = go (newHasher seed) 0 0
  where
    total = BS.length input
    go !hasher !off !chunkIndex
      | off == total = hasher
      | otherwise =
          let size = chunkSize patternIndex chunkIndex (total - off)
              chunk = BS.take size (BS.drop off input)
           in go (update hasher chunk) (off + size) (chunkIndex + 1)

streamingTest :: IO ()
streamingTest = do
  let lengths = [0 .. 640] ++ [895, 896, 897, 1023, 1024, 1025, 1343, 1344, 1345, 4095, 4096, 4097, 20000]
  forM_ [0, k, 0xDEADBEEFCAFEBABE] $ \seed ->
    forM_ lengths $ \len -> do
      let input = patternA len
          expected = hayahash128 input seed
      forM_ [0 .. 7] $ \split -> do
        let hasher = feed seed split input
            label = "stream len=" ++ show len ++ " seed=" ++ hex64 seed ++ " split=" ++ show split
        assertEqual (label ++ " length") (fromIntegral len) (hasherLength hasher)
        assertWord64 (label ++ " h64") (hashLow64 expected) (digest64 hasher)
        assertEqual (label ++ " h128") expected (digest128 hasher)

continuationTest :: IO ()
continuationTest = do
  let input = patternA 2000
  forM_ [0, 1, 63, 64, 447, 448, 449, 1000, 2000] $ \cut -> do
    let prefix = update (newHasher 7) (BS.take cut input)
        expectedPrefix = hayahash64 (BS.take cut input) 7
        complete = update prefix (BS.drop cut input)
    assertWord64 ("prefix cut=" ++ show cut) expectedPrefix (digest64 prefix)
    assertWord64 ("repeat cut=" ++ show cut) expectedPrefix (digest64 prefix)
    assertWord64 ("continue cut=" ++ show cut) (hayahash64 input 7) (digest64 complete)

read32LE :: BS.ByteString -> Int -> Word32
read32LE bytes off =
  foldl' (.|.) 0
    [ fromIntegral (BS.index bytes (off + i)) `shiftL` (8 * i)
      | i <- [0 .. 3]
    ]

read64LE :: BS.ByteString -> Int -> Word64
read64LE bytes off =
  foldl' (.|.) 0
    [ fromIntegral (BS.index bytes (off + i)) `shiftL` (8 * i)
      | i <- [0 .. 7]
    ]

differentialTest :: IO ()
differentialTest = do
  corpusPath <- lookupEnv "HAYAHASH_CORPUS"
  case corpusPath of
    Nothing -> putStrLn "HAYAHASH_CORPUS is unset; skipping differential corpus"
    Just path -> do
      corpus <- BS.readFile path
      assertEqual "differential magic" (BSC.pack "HAYAFZ02") (BS.take 8 corpus)
      let caseCount = fromIntegral (read32LE corpus 8) :: Int
          prngSeed = read64LE corpus 12
          end = checkCases corpus prngSeed caseCount 0 20
      assertEqual "differential trailing bytes" (BS.length corpus) end
      putStrLn ("Haskell matched " ++ show caseCount ++ " C-reference cases")

checkCases :: BS.ByteString -> Word64 -> Int -> Int -> Int -> Int
checkCases !_ !_ 0 !_ !off = off
checkCases !corpus !prngSeed !remaining !caseIndex !off =
  let !len = fromIntegral (read32LE corpus off)
      !seed = read64LE corpus (off + 4)
      !expectedLow = read64LE corpus (off + 12)
      !expectedHigh = read64LE corpus (off + 20)
      !inputOff = off + 28
      !next = inputOff + len
      !input = BS.take len (BS.drop inputOff corpus)
      !actual = hayahash128 input seed
      !label =
        "differential case=" ++ show caseIndex
          ++ " len=" ++ show len
          ++ " seed=" ++ hex64 seed
          ++ " corpus_seed=" ++ hex64 prngSeed
   in if hashLow64 actual /= expectedLow
        then error (label ++ " low: got " ++ hex64 (hashLow64 actual) ++ ", want " ++ hex64 expectedLow)
        else
          if hashHigh64 actual /= expectedHigh
            then error (label ++ " high: got " ++ hex64 (hashHigh64 actual) ++ ", want " ++ hex64 expectedHigh)
            else
              if hayahash64 input seed /= expectedLow
                then error (label ++ " hayahash64 differs from low word")
                else checkCases corpus prngSeed (remaining - 1) (caseIndex + 1) next

publishedStreamingTest :: IO ()
publishedStreamingTest =
  forM_ vectors $ \(len, expected) ->
    assertWord64
      ("published bytewise len=" ++ show len)
      expected
      (digest64 (feed 0 0 (patternA len)))
  where
    vectors =
      [ (0, 0x68AC507CF298CA3F),
        (5, 0x37EE1F8B5A98B84B),
        (10, 0xE28B66FB1E4CB4EA),
        (15, 0x9A8920A57F119D6B),
        (20, 0xC311E14FF31FB2BF),
        (25, 0xC27FDE4AC86CCE54),
        (30, 0x16CC1E65CA2CB4F3),
        (35, 0x1C6522BDC246DA12),
        (40, 0xD110128D567CB9F8)
      ]

main :: IO ()
main = do
  knownAnswerTest
  literalTest
  verificationTest
  streamingTest
  continuationTest
  publishedStreamingTest
  differentialTest
  putStrLn "All Haskell hayahash tests passed."
