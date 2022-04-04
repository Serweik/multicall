#pragma once

#include "multicall.h"
#include <string>

class SenderInterface {
public:
	MC_DECLARE_INTERFACE(SenderInterface)

	virtual ~SenderInterface() = default;

	MC_DECLARE_SIGNAL(tick(int arg))
	MC_DECLARE_SIGNAL(tick(int arg, std::string text))
};

class Factory {
public:
	enum SenderType {
		Type1,
		Type2
	};

	std::shared_ptr<SenderInterface> createSender(SenderType type);
};