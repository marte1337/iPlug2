/* iPlug2 local modification: bounded latency request/adoption/notification handoff.
   See LICENSE.txt. No host calls or storage ownership transfer on the audio thread. */
#pragma once
#include <atomic>
#include <cstdint>

namespace iplug {
class LatencyState
{
public:
  using Word = std::uint64_t;
  static_assert(std::atomic<Word>::is_always_lock_free, "latency handoff must be lock free");
  static Word Pack(int samples, std::uint32_t tag = 0) noexcept
  { return (Word(tag) << 32) | std::uint32_t(samples); }
  static int Samples(Word word) noexcept { return int(std::uint32_t(word)); }
  static std::uint32_t Tag(Word word) noexcept { return std::uint32_t(word >> 32); }
  explicit LatencyState(int samples = 0) noexcept
  : mRequest(Pack(samples)), mPermit(Pack(samples)), mReported(Pack(samples)),
    mAcknowledged(Pack(samples)), mAudio(Pack(samples)) {}
  // One publisher: audio/lifecycle code. No host interaction.
  void Request(int samples, std::uint32_t tag = 0) noexcept
  { mRequest.store(Pack(samples, tag), std::memory_order_release); }
  Word Requested() const noexcept { return mRequest.load(std::memory_order_acquire); }
  Word Reported() const noexcept { return mReported.load(std::memory_order_acquire); }
  Word Acknowledged() const noexcept { return mAcknowledged.load(std::memory_order_acquire); }
  // Control thread authorizes audio-owned adoption, without waiting for a host
  // lifecycle. The settled value is published BEFORE notifying every format.
  void Permit(Word word) noexcept { mPermit.store(word, std::memory_order_release); }
  void Acknowledge(Word word) noexcept { mAcknowledged.store(word, std::memory_order_release); }
  // Audio owner. Finish the current fade, then adopt only the latest permit.
  // Publication stays pinned until the control notification has returned, so
  // synchronous property queries cannot observe a different announcement.
  bool BeginBlock(unsigned fadeSamples) noexcept
  {
    if (mRemaining || Reported() != Acknowledged()) return false;
    const auto next = mPermit.load(std::memory_order_acquire);
    if (next == mAudio) return false;
    const bool timeChange = Samples(next) != Samples(mAudio);
    mAudio = next;
    mRemaining = timeChange ? fadeSamples : 0;
    if (!mRemaining && mAudio == mPermit.load(std::memory_order_acquire))
      mReported.store(mAudio, std::memory_order_release);
    return true;
  }
  void Advance(unsigned samples) noexcept
  {
    if (!mRemaining) return;
    mRemaining = samples >= mRemaining ? 0 : mRemaining - samples;
    if (!mRemaining && mAudio == mPermit.load(std::memory_order_acquire))
      mReported.store(mAudio, std::memory_order_release);
  }
  Word AudioTarget() const noexcept { return mAudio; } // audio thread only
  unsigned Remaining() const noexcept { return mRemaining; }
private:
  std::atomic<Word> mRequest, mPermit, mReported, mAcknowledged;
  Word mAudio;
  unsigned mRemaining = 0;
};
}
