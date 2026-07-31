#include "SpiderSolitaireHack.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <exception>
#include <algorithm>
#include <array>
#include <atomic>

#include "memoryeditor.hpp"
#include "safetyhook_manager.hpp"

using namespace MemoryEditor;

namespace spider_solitaire
{
    namespace
    {
		constexpr char kFreeMovePatchName[] = "SetFreeMovePatch";
		constexpr char kFreePickPatchName[] = "SetFreePickPatch";
		constexpr char kNoMoveCountPatchName[] = "SetNoMoveCountPatch";
		constexpr char kNoScoreDeductionPatchName[] = "SetNoScoreDeductionPatch";
		constexpr char kAutoCollectHookName[] = "AutoCollectHook";
		constexpr char kOrderedDealHookName[] = "OrderedDealHook";
		constexpr char kFaceUpHookName[] = "SetFaceUpHook";

        constexpr char kFaceUpPattern[] =
            "48 8B 88 40 01 00 00 45 33 C0 33 D2 48 8B 0C 0B E8 ?? ?? ?? ??";
        constexpr char kFreeMovePattern[] =
            "74 14 8B 4E 08";
        constexpr char kFreePickPattern[] =
            "44 3B E0 74 03 40 8A FB";
        constexpr char kNoMoveCountPattern[] =
            "FF 40 10 83 B9 F8 00 00 00 00";
        constexpr char kNoScoreDeductionPattern[] =
            "41 FF 4B 14 48 8B CE";
		constexpr char kOrderedDealPattern[] =
			"EB 05 48 8B 7C 24 48 45 33 C0";

        constexpr std::uintptr_t kFaceUpTargetRva = 0x72318;
		constexpr std::size_t kFaceUpSkipLength = 5;
		constexpr std::uintptr_t kAutoCollectFunctionRva = 0x365C4;
		constexpr std::uintptr_t kAutoCollectCountOffset = 0x130;
		constexpr std::int32_t kAutoCollectThreshold = 13;
		constexpr std::uintptr_t kGameStateBaseRva = 0xB5F78;
		constexpr std::uintptr_t kGameStateOffset = 8;
		constexpr std::uintptr_t kCommonStateOffset = 0xE8;
		constexpr std::uintptr_t kScoreOffset = 0x14;
		constexpr std::uintptr_t kMovesOffset = 0x10;
		constexpr std::uintptr_t kGameWindowRva = 0xB6008;
		constexpr UINT kUpdateStatusMessage = 0x464;
		constexpr WPARAM kMovesTextId = 2;
		constexpr WPARAM kScoreTextId = 0x101;
		constexpr std::uintptr_t kOrderedDealArraySizeOffset = 0x130;
		constexpr std::uintptr_t kOrderedDealArrayPointerOffset = 0x140;
		constexpr std::uintptr_t kOrderedDealCardValueOffset = 0x8;
		constexpr std::size_t kOrderedDealMaxCards = 104;
		
		struct OrderedCardEntry
		{
			std::uintptr_t pointer{};
			std::uint32_t order{};
			std::size_t originalIndex{};
		};
    }

    class SpiderSolitaireHack::Impl final
    {
    public:
		[[nodiscard]] HackResult EnableFaceUp()
		{
			if (hookManager_.contains(kFaceUpHookName))
			{
				const bool wasEnabled =
					faceUpEnabled_.exchange(
						true,
						std::memory_order_relaxed);

				if (!wasEnabled)
				{
					faceUpNeedsUpdate_.store(
						true,
						std::memory_order_release);
				}

				return wasEnabled
					? HackResult::AlreadyEnabled
					: HackResult::Success;
			}

			const std::uintptr_t address =
				PatternScan(Pattern(kFaceUpPattern), Offset(16))
					.Scan<std::uintptr_t>(ScanTarget::CurrentModule());

			if (address == 0)
				return HackResult::PatternNotFound;

			const std::uintptr_t moduleBase = GetModuleBase();

			if (moduleBase == 0)
				return HackResult::PatternNotFound;

			faceUpResumeAddress_ =
				address + kFaceUpSkipLength;

			faceUpTarget_ =
				reinterpret_cast<FaceUpTargetFunction>(
					moduleBase + kFaceUpTargetRva);

			faceUpEnabled_.store(
				false,
				std::memory_order_relaxed);

			const auto result =
				hookManager_.install_mid(
					kFaceUpHookName,
					address,
					&Impl::FaceUpHook);

			if (!result)
			{
				faceUpEnabled_.store(
					false,
					std::memory_order_relaxed);

				faceUpResumeAddress_ = 0;
				faceUpTarget_ = nullptr;

				return HackResult::PatchWriteFailed;
			}

			faceUpEnabled_.store(
				true,
				std::memory_order_relaxed);

			faceUpNeedsUpdate_.store(
				true,
				std::memory_order_release);

			return HackResult::Success;
		}

		[[nodiscard]] HackResult DisableFaceUp()
		{
			if (!hookManager_.contains(kFaceUpHookName))
				return HackResult::NotInstalled;

			const bool wasEnabled =
				faceUpEnabled_.exchange(
					false,
					std::memory_order_relaxed);

			if (wasEnabled)
			{
				faceUpNeedsUpdate_.store(
					true,
					std::memory_order_release);
			}

			return wasEnabled
				? HackResult::Success
				: HackResult::AlreadyDisabled;
		}

        [[nodiscard]] HackResult EnableFreeMove()
        {
            if (patchManager_.Has(kFreeMovePatchName))
            {
                if (patchManager_.IsEnabled(kFreeMovePatchName))
                    return HackResult::AlreadyEnabled;

                return patchManager_.Enable(kFreeMovePatchName)
                    ? HackResult::Success
                    : HackResult::PatchEnableFailed;
            }

			const std::uintptr_t address =
				PatternScan(Pattern(kFreeMovePattern))
					.Scan<std::uintptr_t>(ScanTarget::CurrentModule());

			if (address == 0)
				return HackResult::PatternNotFound;

            return patchManager_.Add(
                       kFreeMovePatchName,
                       address,
                       "EB 14 8B 4E 08")
                ? HackResult::Success
                : HackResult::PatchWriteFailed;
        }

        [[nodiscard]] HackResult DisableFreeMove()
        {
            if (!patchManager_.Has(kFreeMovePatchName))
                return HackResult::NotInstalled;

            if (!patchManager_.IsEnabled(kFreeMovePatchName))
                return HackResult::AlreadyDisabled;

            return patchManager_.Disable(kFreeMovePatchName)
                ? HackResult::Success
                : HackResult::PatchDisableFailed;
        }

        [[nodiscard]] HackResult EnableFreePick()
        {
            if (patchManager_.Has(kFreePickPatchName))
            {
                if (patchManager_.IsEnabled(kFreePickPatchName))
                    return HackResult::AlreadyEnabled;

                return patchManager_.Enable(kFreePickPatchName)
                    ? HackResult::Success
                    : HackResult::PatchEnableFailed;
            }

			const std::uintptr_t address =
				PatternScan(Pattern(kFreePickPattern))
					.Scan<std::uintptr_t>(ScanTarget::CurrentModule());

			if (address == 0)
				return HackResult::PatternNotFound;

            return patchManager_.Add(
                       kFreePickPatchName,
                       address,
                       "44 3B E0 74 03 90 90 90")
                ? HackResult::Success
                : HackResult::PatchWriteFailed;
        }

        [[nodiscard]] HackResult DisableFreePick()
        {
            if (!patchManager_.Has(kFreePickPatchName))
                return HackResult::NotInstalled;

            if (!patchManager_.IsEnabled(kFreePickPatchName))
                return HackResult::AlreadyDisabled;

            return patchManager_.Disable(kFreePickPatchName)
                ? HackResult::Success
                : HackResult::PatchDisableFailed;
        }

        [[nodiscard]] HackResult EnableNoMoveCount()
        {
            if (patchManager_.Has(kNoMoveCountPatchName))
            {
                if (patchManager_.IsEnabled(kNoMoveCountPatchName))
                    return HackResult::AlreadyEnabled;

                return patchManager_.Enable(kNoMoveCountPatchName)
                    ? HackResult::Success
                    : HackResult::PatchEnableFailed;
            }

			const std::uintptr_t address =
				PatternScan(Pattern(kNoMoveCountPattern))
					.Scan<std::uintptr_t>(ScanTarget::CurrentModule());

			if (address == 0)
				return HackResult::PatternNotFound;

            return patchManager_.Add(
                       kNoMoveCountPatchName,
                       address,
                       "90 90 90 83 B9 F8 00 00 00 00")
                ? HackResult::Success
                : HackResult::PatchWriteFailed;
        }

        [[nodiscard]] HackResult DisableNoMoveCount()
        {
            if (!patchManager_.Has(kNoMoveCountPatchName))
                return HackResult::NotInstalled;

            if (!patchManager_.IsEnabled(kNoMoveCountPatchName))
                return HackResult::AlreadyDisabled;

            return patchManager_.Disable(kNoMoveCountPatchName)
                ? HackResult::Success
                : HackResult::PatchDisableFailed;
        }
		
        [[nodiscard]] HackResult EnableNoScoreDeduction()
        {
            if (patchManager_.Has(kNoScoreDeductionPatchName))
            {
                if (patchManager_.IsEnabled(kNoScoreDeductionPatchName))
                    return HackResult::AlreadyEnabled;

                return patchManager_.Enable(kNoScoreDeductionPatchName)
                    ? HackResult::Success
                    : HackResult::PatchEnableFailed;
            }

			const std::uintptr_t address =
				PatternScan(Pattern(kNoScoreDeductionPattern))
					.Scan<std::uintptr_t>(ScanTarget::CurrentModule());

			if (address == 0)
				return HackResult::PatternNotFound;

            return patchManager_.Add(
                       kNoScoreDeductionPatchName,
                       address,
                       "90 90 90 90 48 8B CE")
                ? HackResult::Success
                : HackResult::PatchWriteFailed;
        }

        [[nodiscard]] HackResult DisableNoScoreDeduction()
        {
            if (!patchManager_.Has(kNoScoreDeductionPatchName))
                return HackResult::NotInstalled;

            if (!patchManager_.IsEnabled(kNoScoreDeductionPatchName))
                return HackResult::AlreadyDisabled;

            return patchManager_.Disable(kNoScoreDeductionPatchName)
                ? HackResult::Success
                : HackResult::PatchDisableFailed;
        }
		
		[[nodiscard]] HackResult WinNow()
		{
			try
			{
				const std::uintptr_t address =
					CalculatePointerChain(kGameStateBaseRva, {kGameStateOffset});

				if (address == 0)
					return HackResult::PointerChainFailed;

				return WriteValue<std::int32_t>(
						   address,
						   static_cast<std::int32_t>(8))
					? HackResult::Success
					: HackResult::MemoryWriteFailed;
			}
			catch (const std::exception&)
			{
				return HackResult::PointerChainFailed;
			}
		}
		
		[[nodiscard]] HackResult EnableAutoCollect()
		{
			if (hookManager_.contains(kAutoCollectHookName))
			{
				if (hookManager_
						.enabled(kAutoCollectHookName)
						.value_or(false))
				{
					return HackResult::AlreadyEnabled;
				}

				return hookManager_.enable(kAutoCollectHookName)
					? HackResult::Success
					: HackResult::PatchEnableFailed;
			}

			const std::uintptr_t moduleBase =
				GetModuleBase();

			if (moduleBase == 0)
				return HackResult::PatternNotFound;

			const std::uintptr_t targetAddress =
				moduleBase + kAutoCollectFunctionRva;

			const auto installResult =
				hookManager_.install_inline(
					kAutoCollectHookName,
					targetAddress,
					&Impl::AutoCollectDetour,
					false);

			if (!installResult)
				return HackResult::PatchWriteFailed;

			autoCollectOriginal_ =
				hookManager_.original<AutoCollectFunction>(
					kAutoCollectHookName);

			if (autoCollectOriginal_ == nullptr)
			{
				(void)hookManager_.uninstall(kAutoCollectHookName);
				return HackResult::PatchWriteFailed;
			}

			const auto enableResult =
				hookManager_.enable(kAutoCollectHookName);

			if (!enableResult)
			{
				(void)hookManager_.uninstall(kAutoCollectHookName);
				autoCollectOriginal_ = nullptr;

				return HackResult::PatchEnableFailed;
			}

			return HackResult::Success;
		}

		[[nodiscard]] HackResult DisableAutoCollect()
		{
			if (!hookManager_.contains(kAutoCollectHookName))
				return HackResult::NotInstalled;

			if (!hookManager_
					.enabled(kAutoCollectHookName)
					.value_or(false))
			{
				return HackResult::AlreadyDisabled;
			}

			return hookManager_.disable(kAutoCollectHookName)
				? HackResult::Success
				: HackResult::PatchDisableFailed;
		}

		[[nodiscard]] HackResult SetScore(int score)
		{
			if (score < 0)
				return HackResult::InvalidValue;

			try
			{
				const std::uintptr_t scoreAddress =
					CalculatePointerChain(
						kGameStateBaseRva,
						{
							kCommonStateOffset,
							kScoreOffset,
						});

				if (!WriteValue<std::int32_t>(
						scoreAddress,
						static_cast<std::int32_t>(score)))
				{
					return HackResult::MemoryWriteFailed;
				}

				std::array<wchar_t, 64> text{};

				const int result =
					std::swprintf(
						text.data(),
						text.size(),
						L"得分: %d",
						score);

				if (result >= 0)
				{
					UpdateGameStatusText(
						kScoreTextId,
						text.data());
				}

				return HackResult::Success;
			}
			catch (...)
			{
				return HackResult::PointerChainFailed;
			}
		}

		[[nodiscard]] HackResult SetMoves(int moves)
		{
			if (moves < 0)
				return HackResult::InvalidValue;

			try
			{
				const std::uintptr_t movesAddress =
					CalculatePointerChain(
						kGameStateBaseRva,
						{
							kCommonStateOffset,
							kMovesOffset,
						});

				if (!WriteValue<std::int32_t>(
						movesAddress,
						static_cast<std::int32_t>(moves)))
				{
					return HackResult::MemoryWriteFailed;
				}

				std::array<wchar_t, 64> text{};

				const int result =
					std::swprintf(
						text.data(),
						text.size(),
						L"移牌: %4d",
						moves);

				if (result >= 0)
				{
					UpdateGameStatusText(
						kMovesTextId,
						text.data());
				}

				return HackResult::Success;
			}
			catch (...)
			{
				return HackResult::PointerChainFailed;
			}
		}

		[[nodiscard]] HackResult EnableOrderedDeal()
		{
			if (hookManager_.contains(kOrderedDealHookName))
			{
				const bool enabled =
					hookManager_
						.enabled(kOrderedDealHookName)
						.value_or(false);

				if (enabled)
					return HackResult::AlreadyEnabled;

				return hookManager_.enable(
						   kOrderedDealHookName)
					? HackResult::Success
					: HackResult::PatchEnableFailed;
			}

			const std::uintptr_t address =
				PatternScan(Pattern(kOrderedDealPattern))
					.Scan<std::uintptr_t>(
						ScanTarget::CurrentModule());

			if (address == 0)
				return HackResult::PatternNotFound;

			const auto result =
				hookManager_.install_mid(
					kOrderedDealHookName,
					address,
					&OrderedDealHook);

			return result
				? HackResult::Success
				: HackResult::PatchWriteFailed;
		}
		
		[[nodiscard]] HackResult DisableOrderedDeal()
		{
			if (!hookManager_.contains(kOrderedDealHookName))
				return HackResult::NotInstalled;

			const bool enabled =
				hookManager_
					.enabled(kOrderedDealHookName)
					.value_or(false);

			if (!enabled)
				return HackResult::AlreadyDisabled;

			return hookManager_.disable(
					   kOrderedDealHookName)
				? HackResult::Success
				: HackResult::PatchDisableFailed;
		}

		void ResetAll() noexcept
		{
			faceUpEnabled_.store(
				false,
				std::memory_order_relaxed);

			faceUpNeedsUpdate_.store(
				false,
				std::memory_order_relaxed);

			hookManager_.uninstall_all();

			faceUpTarget_ = nullptr;
			faceUpResumeAddress_ = 0;
			autoCollectOriginal_ = nullptr;

			patchManager_.RemoveAll();
		}

    private:
		using FaceUpTargetFunction = void (*)(void* cardObject, bool someFlag1, bool someFlag2);

		inline static FaceUpTargetFunction faceUpTarget_ = nullptr;
		inline static std::uintptr_t faceUpResumeAddress_ = 0;
		inline static std::atomic_bool faceUpEnabled_{false};
		inline static std::atomic_bool faceUpNeedsUpdate_{false};

		static void FaceUpHook(SafetyHookContext& ctx) noexcept
		{
			const bool enabled =
				faceUpEnabled_.load(
					std::memory_order_relaxed);

			if (faceUpNeedsUpdate_.exchange(
					false,
					std::memory_order_acq_rel))
			{
				try
				{
					if (ctx.rsi != 0)
					{
						const std::uintptr_t pointerArray =
							ReadValue<std::uintptr_t>(
								ctx.rsi + 0x68);

						if (pointerArray != 0)
						{
							const int value =
								enabled ? 23 : 7;

							for (int i = 0; i < 10; ++i)
							{
								const std::uintptr_t objectPointer =
									ReadValue<std::uintptr_t>(
										pointerArray + i * 8);

								if (objectPointer == 0)
									continue;

								(void)WriteValue<int>(
									objectPointer + 0x1C,
									value);
							}
						}
					}
				}
				catch (...)
				{
				}
			}

			if (!enabled)
				return;

			if (faceUpResumeAddress_ == 0 ||
				faceUpTarget_ == nullptr)
			{
				return;
			}

			ctx.rip = faceUpResumeAddress_;

			try
			{
				const std::uintptr_t cardObject =
					ctx.rcx;

				faceUpTarget_(
					reinterpret_cast<void*>(cardObject),
					true,
					true);
			}
			catch (...)
			{
			}
		}

		using AutoCollectFunction =
			int (*)(void* spiderGame, void* pile);

		static int AutoCollectDetour(
			void* spiderGame,
			void* pile) noexcept
		{
			if (pile != nullptr)
			{
				try
				{
					const std::uintptr_t pileAddress =
						reinterpret_cast<std::uintptr_t>(pile);

					const std::int32_t count =
						ReadValue<std::int32_t>(
							pileAddress +
							kAutoCollectCountOffset);

					if (count >= kAutoCollectThreshold)
						return 1;
				}
				catch (...)
				{
				}
			}

			const AutoCollectFunction original =
				autoCollectOriginal_;

			if (original == nullptr)
			{
				return 0;
			}

			return original(spiderGame, pile);
		}

		inline static AutoCollectFunction
			autoCollectOriginal_ = nullptr;

		[[nodiscard]] static HWND GetGameWindow() noexcept
		{
			try
			{
				const std::uintptr_t moduleBase =
					GetModuleBase();

				if (moduleBase == 0)
					return nullptr;

				const std::uintptr_t windowValue =
					ReadValue<std::uintptr_t>(
						moduleBase + kGameWindowRva);

				const HWND window =
					reinterpret_cast<HWND>(windowValue);

				return IsWindow(window)
					? window
					: nullptr;
			}
			catch (...)
			{
				return nullptr;
			}
		}

		static void UpdateGameStatusText(
			WPARAM textId,
			const wchar_t* text) noexcept
		{
			if (text == nullptr)
				return;

			const HWND gameWindow =
				GetGameWindow();

			if (gameWindow == nullptr)
				return;

			SendMessageW(
				gameWindow,
				kUpdateStatusMessage,
				textId,
				reinterpret_cast<LPARAM>(text));
		}

		[[nodiscard]] static std::uint32_t GetOrderedDealKey(
			std::int32_t cardValue) noexcept
		{
			std::int32_t remainder =
				cardValue % 13;

			if (remainder < 0)
				remainder += 13;

			return static_cast<std::uint32_t>(
				(remainder + 1) % 13);
		}

		static void OrderedDealHook(
			SafetyHookContext& context) noexcept
		{
			try
			{
				const std::uintptr_t dealObject =
					context.r13;

				if (dealObject == 0)
					return;

				const std::int32_t rawCount =
					ReadValue<std::int32_t>(
						dealObject +
						kOrderedDealArraySizeOffset);

				if (rawCount <= 1)
					return;

				if (rawCount >
					static_cast<std::int32_t>(
						kOrderedDealMaxCards))
				{
					return;
				}

				const std::size_t count =
					static_cast<std::size_t>(rawCount);

				const std::uintptr_t pointerArray =
					ReadValue<std::uintptr_t>(
						dealObject +
						kOrderedDealArrayPointerOffset);

				if (pointerArray == 0)
					return;

				std::array<
					OrderedCardEntry,
					kOrderedDealMaxCards>
					cards{};

				for (std::size_t index = 0;
					 index < count;
					 ++index)
				{
					const std::uintptr_t elementAddress =
						pointerArray +
						index * sizeof(std::uintptr_t);

					const std::uintptr_t cardPointer =
						ReadValue<std::uintptr_t>(
							elementAddress);

					if (cardPointer == 0)
						return;

					const std::int32_t cardValue =
						ReadValue<std::int32_t>(
							cardPointer +
							kOrderedDealCardValueOffset);

					cards[index] = OrderedCardEntry{
						.pointer = cardPointer,
						.order = GetOrderedDealKey(cardValue),
						.originalIndex = index,
					};
				}

				std::sort(
					cards.begin(),
					cards.begin() +
						static_cast<std::ptrdiff_t>(count),
					[](const OrderedCardEntry& left,
					   const OrderedCardEntry& right)
					{
						if (left.order != right.order)
							return left.order < right.order;

						return left.originalIndex <
							right.originalIndex;
					});

				for (std::size_t index = 0;
					 index < count;
					 ++index)
				{
					const std::uintptr_t elementAddress =
						pointerArray +
						index * sizeof(std::uintptr_t);

					if (!WriteValue<std::uintptr_t>(
							elementAddress,
							cards[index].pointer))
					{
						return;
					}
				}
			}
			catch (...)
			{
			}
		}

		PatchManager patchManager_{};

		safetyhook_manager::HookManager
			hookManager_{};
    };

    SpiderSolitaireHack::SpiderSolitaireHack()
        : impl_(std::make_unique<Impl>())
    {
    }

    SpiderSolitaireHack::~SpiderSolitaireHack()
    {
        ResetAll();
    }

    HackResult SpiderSolitaireHack::EnableFreeMove()
    {
        return impl_->EnableFreeMove();
    }

    HackResult SpiderSolitaireHack::DisableFreeMove()
    {
        return impl_->DisableFreeMove();
    }

    HackResult SpiderSolitaireHack::EnableFreePick()
    {
        return impl_->EnableFreePick();
    }

    HackResult SpiderSolitaireHack::DisableFreePick()
    {
        return impl_->DisableFreePick();
    }

    HackResult SpiderSolitaireHack::EnableFaceUp()
    {
        return impl_->EnableFaceUp();
    }

    HackResult SpiderSolitaireHack::DisableFaceUp()
    {
        return impl_->DisableFaceUp();
    }

	HackResult SpiderSolitaireHack::EnableOrderedDeal()
	{
		return impl_->EnableOrderedDeal();
	}

	HackResult SpiderSolitaireHack::DisableOrderedDeal()
	{
		return impl_->DisableOrderedDeal();
	}

    HackResult SpiderSolitaireHack::EnableNoMoveCount()
    {
        return impl_->EnableNoMoveCount();
    }

    HackResult SpiderSolitaireHack::DisableNoMoveCount()
    {
        return impl_->DisableNoMoveCount();
    }

    HackResult SpiderSolitaireHack::EnableNoScoreDeduction()
    {
        return impl_->EnableNoScoreDeduction();
    }

    HackResult SpiderSolitaireHack::DisableNoScoreDeduction()
    {
        return impl_->DisableNoScoreDeduction();
    }

	HackResult SpiderSolitaireHack::EnableAutoCollect()
	{
		return impl_->EnableAutoCollect();
	}

	HackResult SpiderSolitaireHack::DisableAutoCollect()
	{
		return impl_->DisableAutoCollect();
	}

    HackResult SpiderSolitaireHack::WinNow()
    {
        return impl_->WinNow();
    }

	HackResult SpiderSolitaireHack::SetScore(int score)
	{
		return impl_->SetScore(score);
	}

	HackResult SpiderSolitaireHack::SetMoves(int moves)
	{
		return impl_->SetMoves(moves);
	}

    void SpiderSolitaireHack::ResetAll() noexcept
    {
        if (impl_)
            impl_->ResetAll();
    }
}
