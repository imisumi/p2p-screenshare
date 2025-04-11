#pragma once

#include "Log.h"
#include "MyAssert.h"

class ScopedTimer
{
public:
	enum class TimeType
	{
		Microseconds,
		Milliseconds,
		Seconds,
	};

	ScopedTimer(const std::string &name, TimeType type = TimeType::Milliseconds)
		: m_Name(name), m_Type(type)
	{
		m_StartTime = std::chrono::high_resolution_clock::now();
	}

	~ScopedTimer()
	{
		auto endTime = std::chrono::high_resolution_clock::now();

		switch (m_Type)
		{
		case TimeType::Microseconds:
		{
			auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_StartTime);
			LOG_INFO("{}: {}us", m_Name, duration.count());
			break;
		}
		case TimeType::Milliseconds:
		{
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - m_StartTime);
			LOG_INFO("{}: {}ms", m_Name, duration.count());
			break;
		}
		case TimeType::Seconds:
		{
			auto duration = std::chrono::duration_cast<std::chrono::duration<float>>(endTime - m_StartTime);
			LOG_INFO("{}: {:.3f}s", m_Name, duration.count());
			break;
		}
		}
	}

private:
	std::string m_Name;
	std::chrono::high_resolution_clock::time_point m_StartTime;
	TimeType m_Type;
};
