#include "../darkspore_server/source/game/cashout_rewards.h"
#include <cassert>
#include <cstdint>

using namespace Game::MissionResults;
static_assert(CashOutRewardCount(0) == 0);
static_assert(CashOutRewardCount(1) == 1);
static_assert(CashOutRewardCount(4) == 4);
static_assert(CashOutRewardCount(5) == 4);
static_assert(CashOutRewardCount(255) == 4);
static_assert(CashOutRewardLevel(0, 1) == 0);
static_assert(CashOutRewardLevel(1, 0) == 0);
static_assert(CashOutRewardLevel(1, 1) == 11);
static_assert(CashOutRewardLevel(1, 4) == 14);
static_assert(CashOutRewardLevel(4, 1) == 20);
static_assert(CashOutRewardLevel(5, 1) == 21);
static_assert(CashOutRewardLevel(5, 4) == 24);
static_assert(CashOutRewardLevel(8, 2) == 30);
static_assert(MakeCashOutReceiptId(0, 42) == 0);
static_assert(MakeCashOutReceiptId(42, 0) == 0);
static_assert(MakeCashOutReceiptId(42, 3) != MakeCashOutReceiptId(43, 3));
static_assert(MakeCashOutReceiptId(42, 3) != MakeCashOutReceiptId(42, 4));
static_assert(CashOutRewardRoll(0, 0) == 0);
static_assert(CashOutRewardRoll(123, 4) == 0);
static_assert(CashOutRewardRarity(0, {20, 2}) == 0);
static_assert(CashOutRewardRarity(101, {20, 2}) == 0);
static_assert(CashOutRewardRarity(80, {20, 2}) == 4);
static_assert(CashOutRewardRarity(81, {20, 2}) == 5);
static_assert(CashOutRewardRarity(98, {20, 2}) == 5);
static_assert(CashOutRewardRarity(99, {20, 2}) == 6);
static_assert(CashOutRewardRarity(75, {20, 21}) == 0);
static_assert(CashOutRewardRarity(1, {0, 0}) == 4);
static_assert(CashOutRigblock(0, 0) == 0);
static_assert(CashOutRigblock(9, 4) == 0);

int main() {
    const auto receipt = MakeCashOutReceiptId(0x123456789abcdef0ULL, 12345);
    assert(receipt != 0);
    for (std::size_t i = 0; i < 4; ++i) {
        auto roll = CashOutRewardRoll(receipt, i);
        assert(roll >= 1 && roll <= 100);
        assert(roll == CashOutRewardRoll(receipt, i));
        assert(CashOutRigblock(receipt, i) >= 1);
        assert(CashOutRigblock(receipt, i) <= 1573);
        assert(CashOutRewardRarity(roll, {35, 5}) >= 4);
        assert(CashOutRewardRarity(roll, {35, 5}) <= 6);
    }
}
