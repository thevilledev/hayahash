{-# LANGUAGE BangPatterns #-}

-- |
-- Module      : Data.Hash.Hayahash
-- Description : Bit-exact hayahash64 and hayahash128
-- License     : Unlicense
--
-- A pure Haskell port of the reference @hayahash.h@ implementation.
-- Inputs are read as bytes in little-endian order, so results do not depend
-- on the host byte order.
module Data.Hash.Hayahash
  ( Hash128 (..),
    hayahash64,
    hayahash128,
    Hasher,
    newHasher,
    update,
    digest64,
    digest128,
    hasherSeed,
    hasherLength,
  )
where

import Data.Bits (rotateL, shiftL, shiftR, xor, (.|.))
import qualified Data.ByteString as BS
import Data.ByteString (ByteString)
import qualified Data.ByteString.Unsafe as BSU
import Data.Word (Word64)

-- | A 128-bit digest represented as low and high 64-bit words.
-- 'hashLow64' is exactly 'hayahash64' for the same input and seed.
data Hash128 = Hash128
  { hashLow64 :: !Word64,
    hashHigh64 :: !Word64
  }
  deriving (Eq, Ord, Show)

-- The multiplier is 2^64 / golden ratio, rounded down and made odd.
k, m1, m2, n1, n2 :: Word64
k = 0x9E3779B97F4A7C15
m1 = 0x3C79AC492BA7B653
m2 = 0x1C69B3F74AC4AE35
n1 = 0xFF51AFD7ED558CCD
n2 = 0xC4CEB9FE1A85EC53

bulkMin, bufferCapacity, keepBytes :: Int
bulkMin = 320
bufferCapacity = 448
keepBytes = 128

-- Word64 arithmetic wraps modulo 2^64, matching uint64_t in the reference.
load32LE :: ByteString -> Int -> Word64
load32LE !bytes !off =
  byte 0
    .|. (byte 1 `shiftL` 8)
    .|. (byte 2 `shiftL` 16)
    .|. (byte 3 `shiftL` 24)
  where
    byte i = fromIntegral (BSU.unsafeIndex bytes (off + i))

load64LE :: ByteString -> Int -> Word64
load64LE !bytes !off =
  load32LE bytes off .|. (load32LE bytes (off + 4) `shiftL` 32)

fmix :: Word64 -> Word64
fmix !value =
  let !x1 = value `xor` (value `shiftR` 27)
      !x2 = x1 * m1
      !x3 = x2 `xor` (x2 `shiftR` 33)
      !x4 = x3 * m2
   in x4 `xor` (x4 `shiftR` 27)

fmix128 :: Word64 -> Word64
fmix128 !value =
  let !x1 = value `xor` (value `shiftR` 30)
      !x2 = x1 * n1
      !x3 = x2 `xor` (x2 `shiftR` 31)
      !x4 = x3 * n2
   in x4 `xor` (x4 `shiftR` 33)

longAvalanche :: Word64 -> Word64
longAvalanche !value =
  let !x1 = value `xor` (value `shiftR` 37)
      !x2 = x1 * k
   in x2 `xor` (x2 `shiftR` 32)

inj :: Word64 -> Word64
inj !word = word `xor` rotateL word 21 `xor` rotateL word 41

inj2 :: Word64 -> Word64
inj2 !word = word `xor` rotateL word 11 `xor` rotateL word 50

injAt :: ByteString -> Int -> Word64
injAt bytes off = inj (load64LE bytes off)

stripe :: ByteString -> Int -> Word64 -> Word64 -> (Word64, Word64)
stripe !bytes !off !lane !previous =
  let !word = load64LE bytes off
      !lane' = (lane `xor` (word + rotateL previous 27)) * k
   in (lane', word)

-- Eight bulk lanes and the previous raw input stripe.
data Lanes = Lanes
  !Word64
  !Word64
  !Word64
  !Word64
  !Word64
  !Word64
  !Word64
  !Word64
  !Word64

initialLanes :: Word64 -> Lanes
initialLanes !seed =
  let !s = seed `xor` k
   in Lanes
        (s `xor` k)
        (rotateL s 17 + (k `shiftL` 21))
        (rotateL s 34 `xor` (k `shiftR` 13))
        (rotateL s 51 + (k `shiftL` 42))
        (s + (k `shiftR` 27))
        (rotateL s 13 `xor` (k `shiftL` 9))
        (rotateL s 26 + (k `shiftR` 40))
        (rotateL s 39 `xor` (k `shiftL` 30))
        0

absorbBlock :: ByteString -> Int -> Lanes -> Lanes
absorbBlock !bytes !off (Lanes h0 h1 h2 h3 h4 h5 h6 h7 wp0) =
  let (!h0', !wp1) = stripe bytes off h0 wp0
      (!h1', !wp2) = stripe bytes (off + 8) h1 wp1
      (!h2', !wp3) = stripe bytes (off + 16) h2 wp2
      (!h3', !wp4) = stripe bytes (off + 24) h3 wp3
      (!h4', !wp5) = stripe bytes (off + 32) h4 wp4
      (!h5', !wp6) = stripe bytes (off + 40) h5 wp5
      (!h6', !wp7) = stripe bytes (off + 48) h6 wp6
      (!h7', !wp8) = stripe bytes (off + 56) h7 wp7
   in Lanes (h0' + wp8) h1' h2' h3' h4' h5' h6' h7' wp8

-- Absorb a byte count that is a multiple of 64.
absorbBlocks :: ByteString -> Int -> Int -> Lanes -> Lanes
absorbBlocks !bytes !off !count !lanes = go off count lanes
  where
    go !_ 0 !state = state
    go !position !remaining !state =
      go (position + 64) (remaining - 64) (absorbBlock bytes position state)

data Terms = Terms !Word64 !Word64 !Word64

shortWords :: ByteString -> (Word64, Word64)
shortWords !bytes
  | len >= 8 = (load64LE bytes 0, load64LE bytes (len - 8))
  | len >= 4 = (load32LE bytes 0, load32LE bytes (len - 4))
  | len > 0 =
      ( fromIntegral (BSU.unsafeIndex bytes 0),
        (fromIntegral (BSU.unsafeIndex bytes (len `shiftR` 1)) `shiftL` 8)
          .|. (fromIntegral (BSU.unsafeIndex bytes (len - 1)) `shiftL` 16)
      )
  | otherwise = (0, 0)
  where
    !len = BS.length bytes

shortTerms :: ByteString -> Word64 -> (Word64, Word64)
shortTerms !bytes !seed =
  let !lenmix = fromIntegral (BS.length bytes) * k
      !s = seed `xor` k
      (!a, !b) = shortWords bytes
      !x = (inj a `xor` s `xor` k) * k
      !y = (inj2 b `xor` rotateL s 23 `xor` (k `shiftR` 19)) * m1
      !u = rotateL x 27 `xor` y `xor` lenmix
   in (x, u)

-- The four-lane mid loop. The returned offset and length identify the tail.
midLoop :: ByteString -> Int -> Int -> Word64 -> Word64 -> Word64 -> Word64 -> Word64 -> (Word64, Word64, Word64, Word64, Word64, Int, Int)
midLoop !bytes = go
  where
    go !off !remaining !h0 !h1 !h2 !h3 !wp
      | remaining >= 32 =
          let (!h0', !wp1) = stripe bytes off h0 wp
              (!h1', !wp2) = stripe bytes (off + 8) h1 wp1
              (!h2', !wp3) = stripe bytes (off + 16) h2 wp2
              (!h3', !wp4) = stripe bytes (off + 24) h3 wp3
           in go (off + 32) (remaining - 32) h0' h1' h2' h3' wp4
      | otherwise = (h0, h1, h2, h3, wp, off, remaining)

finishLower :: ByteString -> Word64 -> Word64 -> Word64 -> Word64 -> Word64 -> Word64 -> Int -> Int -> Terms
finishLower !bytes !seed !h0 !h1 !h2 !h3 !wp !off !remaining =
  let !h0Wall = h0 + rotateL wp 27
      (!h0Tail, !h1Tail, !remainingTail) =
        if remaining > 16
          then
            ( (h0Wall + injAt bytes off) * k,
              (h1 + injAt bytes (off + 8)) * k,
              remaining - 16
            )
          else (h0Wall, h1, remaining)
      !len = BS.length bytes
      (!h2Tail, !h3Tail) =
        if remainingTail > 0
          then
            ( (h2 + injAt bytes (len - 16)) * k,
              (h3 + injAt bytes (len - 8)) * k
            )
          else (h2, h3)
      !lenmix = fromIntegral len * k
      !t0 = (h0Tail `xor` rotateL h1Tail 13 `xor` lenmix) * k
      !t1 = (h2Tail `xor` rotateL h3Tail 33) * k
   in Terms (seed `xor` k) t0 t1

longTerms :: ByteString -> Word64 -> Terms
longTerms !bytes !seed =
  let !len = BS.length bytes
      !start@(Lanes ih0 ih1 ih2 ih3 _ _ _ _ iwp) = initialLanes seed
      (!h0, !h1, !h2, !h3, !wp, !off, !remaining) =
        if len >= bulkMin
          then
            let !blocks = (len `div` 64) * 64
                !(Lanes bh0 bh1 bh2 bh3 bh4 bh5 bh6 bh7 bwp) =
                  absorbBlocks bytes 0 blocks start
             in ( (bh0 `xor` rotateL bh4 11) * k,
                  (bh1 `xor` rotateL bh5 19) * k,
                  (bh2 `xor` rotateL bh6 31) * k,
                  (bh3 `xor` rotateL bh7 47) * k,
                  bwp,
                  blocks,
                  len - blocks
                )
          else (ih0, ih1, ih2, ih3, iwp, 0, len)
      (!mh0, !mh1, !mh2, !mh3, !mwp, !moff, !mremaining) =
        midLoop bytes off remaining h0 h1 h2 h3 wp
   in finishLower bytes seed mh0 mh1 mh2 mh3 mwp moff mremaining

-- | Hash a strict 'ByteString' with a 64-bit seed.
--
-- The result is bit-exact with @hayahash64()@ in the C reference.
hayahash64 :: ByteString -> Word64 -> Word64
hayahash64 !bytes !seed
  | BS.length bytes <= 16 =
      let (!_, !u) = shortTerms bytes seed
       in fmix u
  | otherwise =
      let !(Terms s t0 t1) = longTerms bytes seed
       in longAvalanche (s `xor` t0 `xor` rotateL t1 29)

-- | Hash a strict 'ByteString' once and return both output words.
-- The low word is exactly 'hayahash64' for the same arguments.
hayahash128 :: ByteString -> Word64 -> Hash128
hayahash128 !bytes !seed
  | BS.length bytes <= 16 =
      let (!x, !u) = shortTerms bytes seed
       in Hash128 (fmix u) (fmix128 (x + rotateL u 32))
  | otherwise =
      let !(Terms s t0 t1) = longTerms bytes seed
       in Hash128
            (longAvalanche (s `xor` t0 `xor` rotateL t1 29))
            (fmix128 (rotateL s 32 `xor` (t1 + rotateL t0 47)))

-- | An immutable incremental hash state.
--
-- At most 447 input bytes are retained before bulk hashing begins, and the
-- bulk path retains between 128 and 191 bytes. Calling 'digest64' or
-- 'digest128' does not consume the state, so an old value can be reused to
-- fork a common prefix.
data Hasher = Hasher !Lanes !ByteString !Word64 !Word64 !Bool

-- | Create an empty incremental state with the supplied seed.
newHasher :: Word64 -> Hasher
newHasher !seed = Hasher (initialLanes seed) BS.empty seed 0 False

-- | The seed with which this state was initialized.
hasherSeed :: Hasher -> Word64
hasherSeed (Hasher _ _ seed _ _) = seed

-- | Number of bytes absorbed so far, modulo 2^64.
hasherLength :: Hasher -> Word64
hasherLength (Hasher _ _ _ total _) = total

-- Copy a small slice so the state cannot retain an arbitrarily large input
-- buffer through ByteString's zero-copy slicing.
copyAppend :: ByteString -> ByteString -> ByteString
copyAppend !left !right = BS.copy (left <> right)

-- Feed bytes after the state has committed to the eight-lane bulk path.
-- The retained buffer remains in [128, 191] bytes.
feedBulk :: Lanes -> ByteString -> ByteString -> (Lanes, ByteString)
feedBulk !lanes !buffer !input
  | BS.null input = (lanes, buffer)
  | bufferLen > keepBytes =
      let !needed = 192 - bufferLen
       in if inputLen < needed
            then (lanes, copyAppend buffer input)
            else
              let !joined = copyAppend buffer (BS.take needed input)
                  !lanes' = absorbBlocks joined 0 64 lanes
                  !buffer' = BS.copy (BS.drop 64 joined)
               in feedBulk lanes' buffer' (BS.drop needed input)
  | inputLen > bufferCapacity =
      let !lanes' = absorbBlocks buffer 0 keepBytes lanes
          !direct = ((inputLen - keepBytes) `div` 64) * 64
          !lanes'' = absorbBlocks input 0 direct lanes'
          !buffer' = BS.copy (BS.drop direct input)
       in (lanes'', buffer')
  | otherwise =
      let !joined = copyAppend buffer input
          !joinedLen = BS.length joined
          !consume =
            if joinedLen >= 192
              then ((joinedLen - keepBytes) `div` 64) * 64
              else 0
          !lanes' = absorbBlocks joined 0 consume lanes
          !buffer' = BS.copy (BS.drop consume joined)
       in (lanes', buffer')
  where
    !bufferLen = BS.length buffer
    !inputLen = BS.length input

-- | Absorb another strict 'ByteString'. Empty updates are a no-op.
update :: Hasher -> ByteString -> Hasher
update state@(Hasher lanes buffer seed total bulk) !input
  | BS.null input = state
  | not bulk && bufferLen + inputLen < bufferCapacity =
      Hasher lanes (copyAppend buffer input) seed total' False
  | not bulk =
      let !needed = bufferCapacity - bufferLen
          !prefix = copyAppend buffer (BS.take needed input)
          !lanes' = absorbBlocks prefix 0 (bufferCapacity - keepBytes) lanes
          !buffer' = BS.copy (BS.drop (bufferCapacity - keepBytes) prefix)
          (!lanes'', !buffer'') = feedBulk lanes' buffer' (BS.drop needed input)
       in Hasher lanes'' buffer'' seed total' True
  | otherwise =
      let (!lanes', !buffer') = feedBulk lanes buffer input
       in Hasher lanes' buffer' seed total' True
  where
    !bufferLen = BS.length buffer
    !inputLen = BS.length input
    !total' = total + fromIntegral inputLen

finishBulk :: Lanes -> ByteString -> Word64 -> Word64 -> Terms
finishBulk !lanes !buffer !seed !total =
  let !len = BS.length buffer
      !blocks = (len `div` 64) * 64
      !(Lanes h0 h1 h2 h3 h4 h5 h6 h7 wp) =
        absorbBlocks buffer 0 blocks lanes
      !fh0 = (h0 `xor` rotateL h4 11) * k
      !fh1 = (h1 `xor` rotateL h5 19) * k
      !fh2 = (h2 `xor` rotateL h6 31) * k
      !fh3 = (h3 `xor` rotateL h7 47) * k
      (!mh0, !mh1, !mh2, !mh3, !mwp, !off, !remaining) =
        midLoop buffer blocks (len - blocks) fh0 fh1 fh2 fh3 wp
      !h0Wall = mh0 + rotateL mwp 27
      (!h0Tail, !h1Tail, !remainingTail) =
        if remaining > 16
          then
            ( (h0Wall + injAt buffer off) * k,
              (mh1 + injAt buffer (off + 8)) * k,
              remaining - 16
            )
          else (h0Wall, mh1, remaining)
      (!h2Tail, !h3Tail) =
        if remainingTail > 0
          then
            ( (mh2 + injAt buffer (len - 16)) * k,
              (mh3 + injAt buffer (len - 8)) * k
            )
          else (mh2, mh3)
      !t0 = (h0Tail `xor` rotateL h1Tail 13 `xor` (total * k)) * k
      !t1 = (h2Tail `xor` rotateL h3Tail 33) * k
   in Terms (seed `xor` k) t0 t1

-- | Return the current 64-bit digest without consuming the state.
digest64 :: Hasher -> Word64
digest64 (Hasher lanes buffer seed total bulk)
  | not bulk = hayahash64 buffer seed
  | otherwise =
      let !(Terms s t0 t1) = finishBulk lanes buffer seed total
       in longAvalanche (s `xor` t0 `xor` rotateL t1 29)

-- | Return both current digest words without consuming the state.
digest128 :: Hasher -> Hash128
digest128 (Hasher lanes buffer seed total bulk)
  | not bulk = hayahash128 buffer seed
  | otherwise =
      let !(Terms s t0 t1) = finishBulk lanes buffer seed total
       in Hash128
            (longAvalanche (s `xor` t0 `xor` rotateL t1 29))
            (fmix128 (rotateL s 32 `xor` (t1 + rotateL t0 47)))
