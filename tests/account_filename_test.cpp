#include "../darkspore_server/source/sporenet/account_filename.h"
#include <cassert>
#include <string>

using SporeNet::AccountFiles::IsSafeAccountIdentifier;

static_assert(IsSafeAccountIdentifier("hero@example.com"));
static_assert(IsSafeAccountIdentifier("name+tag@example.net"));
static_assert(IsSafeAccountIdentifier("local_user-01"));
static_assert(!IsSafeAccountIdentifier(""));
static_assert(!IsSafeAccountIdentifier("."));
static_assert(!IsSafeAccountIdentifier(".."));
static_assert(!IsSafeAccountIdentifier("../accounts"));
static_assert(!IsSafeAccountIdentifier("a/../accounts"));
static_assert(!IsSafeAccountIdentifier("C:\\Windows"));
static_assert(!IsSafeAccountIdentifier("bad\\name"));
static_assert(!IsSafeAccountIdentifier("bad?name"));
static_assert(!IsSafeAccountIdentifier("user."));
static_assert(!IsSafeAccountIdentifier("NUL"));
static_assert(!IsSafeAccountIdentifier("CON.xml"));
static_assert(!IsSafeAccountIdentifier("com1"));
static_assert(!IsSafeAccountIdentifier("LPT9.txt"));

int main() {
    assert(!IsSafeAccountIdentifier(std::string(241, 'x')));
    assert(IsSafeAccountIdentifier(std::string(240, 'x')));
}
