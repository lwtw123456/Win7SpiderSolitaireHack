#pragma once

#include <memory>

namespace spider_solitaire
{
	enum class HackResult
	{
		Success,
		AlreadyEnabled,
		AlreadyDisabled,
		NotInstalled,
		NotImplemented,
		PatternNotFound,
		PointerChainFailed,
		MemoryWriteFailed,
		PatchWriteFailed,
		PatchEnableFailed,
		PatchDisableFailed,
		InvalidValue,
	};

    class SpiderSolitaireHack final
    {
    public:
        SpiderSolitaireHack();
        ~SpiderSolitaireHack();

        SpiderSolitaireHack(const SpiderSolitaireHack&) = delete;
        SpiderSolitaireHack& operator=(const SpiderSolitaireHack&) = delete;
        SpiderSolitaireHack(SpiderSolitaireHack&&) = delete;
        SpiderSolitaireHack& operator=(SpiderSolitaireHack&&) = delete;

        [[nodiscard]] HackResult EnableFreeMove();
        [[nodiscard]] HackResult DisableFreeMove();

        [[nodiscard]] HackResult EnableFreePick();
        [[nodiscard]] HackResult DisableFreePick();

        [[nodiscard]] HackResult EnableFaceUp();
        [[nodiscard]] HackResult DisableFaceUp();

        [[nodiscard]] HackResult EnableOrderedDeal();
        [[nodiscard]] HackResult DisableOrderedDeal();

        [[nodiscard]] HackResult EnableNoMoveCount();
        [[nodiscard]] HackResult DisableNoMoveCount();

        [[nodiscard]] HackResult EnableNoScoreDeduction();
        [[nodiscard]] HackResult DisableNoScoreDeduction();

        [[nodiscard]] HackResult EnableAutoCollect();
        [[nodiscard]] HackResult DisableAutoCollect();

        [[nodiscard]] HackResult WinNow();
        [[nodiscard]] HackResult SetScore(int score);
        [[nodiscard]] HackResult SetMoves(int moves);

        void ResetAll() noexcept;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
