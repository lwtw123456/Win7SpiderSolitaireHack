#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <psapi.h>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <unordered_map>
#include <memory>
#include <sstream>


#ifdef _MSC_VER
#pragma comment(lib, "Psapi.lib")
#endif

namespace MemoryEditor
{
	inline constexpr std::size_t MaxPatternLength = 128;
	inline constexpr std::size_t MaxInstructionLength = 16;

	inline constexpr char Char0 = '0';
	inline constexpr char Char9 = '9';
	inline constexpr char CharA = 'A';
	inline constexpr char CharF = 'F';

	inline constexpr std::uint8_t HexValueA = 10;
	inline constexpr std::uint8_t HexPower1 = 0x01;
	inline constexpr std::uint8_t HexPower2 = 0x10;
	
	struct Byte
	{
		bool IsWildcard=false;
		uint8_t Value=0;
	};

	struct Pattern
	{
		Byte Bytes[MaxPatternLength];
		uint64_t Size;

		constexpr Pattern():Bytes(),Size(0){}

		inline constexpr Pattern(const char* aPattern):Bytes(),Size(0)
		{
			uint64_t len=0;

			while(aPattern[len])
			{
				len++;
			}

			uint64_t j=0;

			for(uint64_t i=0;i<len;i++)
			{
				if(Size>=MaxPatternLength)
					throw std::length_error(
						"Pattern exceeds MaxPatternLength."
					);

				if(aPattern[i]==' '||aPattern[i]=='<'||aPattern[i]=='>')
					continue;

				if(aPattern[i]=='?')
				{
					Bytes[j]=Byte{true};
					Size++;
					j++;

					if(i+1<len&&aPattern[i+1]=='?')
						i++;

					continue;
				}

				bool firstValid =
					(aPattern[i] >= Char0 && aPattern[i] <= Char9) ||
					(aPattern[i] >= CharA && aPattern[i] <= CharF);

				if(!firstValid)
					throw "Invalid hexadecimal.";

				uint8_t val=0;
				bool secondValid =
					i + 1 < len &&
					(
						(aPattern[i + 1] >= Char0 && aPattern[i + 1] <= Char9) ||
						(aPattern[i + 1] >= CharA && aPattern[i + 1] <= CharF)
					);

				if(secondValid)
				{
					if(aPattern[i] <= Char9)
						val += (aPattern[i] - Char0) * HexPower2;
					else
						val += ((aPattern[i] - CharA) + HexValueA) * HexPower2;

					if(aPattern[i + 1] <= Char9)
						val += (aPattern[i + 1] - Char0) * HexPower1;
					else
						val += ((aPattern[i + 1] - CharA) + HexValueA) * HexPower1;

					i++;
				}
				else
				{
					if(aPattern[i] <= Char9)
						val += (aPattern[i] - Char0) * HexPower1;
					else
						val += ((aPattern[i] - CharA) + HexValueA) * HexPower1;
				}

				Bytes[j]=Byte{false,val};
				Size++;
				j++;
			}
		}
	};

	inline bool operator==(const Pattern& lhs,const PBYTE rhs)
	{
		for(uint64_t i=0;i<lhs.Size;i++)
		{
			if(!lhs.Bytes[i].IsWildcard&&lhs.Bytes[i].Value!=rhs[i])
				return false;
		}

		return true;
	}

	inline bool operator==(const Pattern& lhs, const Pattern& rhs)
	{
		if(lhs.Size != rhs.Size)
			return false;

		for(std::uint64_t i = 0; i < lhs.Size; ++i)
		{
			if(lhs.Bytes[i].IsWildcard != rhs.Bytes[i].IsWildcard)
				return false;

			if(!lhs.Bytes[i].IsWildcard &&
			   lhs.Bytes[i].Value != rhs.Bytes[i].Value)
				return false;
		}

		return true;
	}

	inline bool operator!=(const Pattern& lhs,const Pattern& rhs)
	{
		return !(lhs==rhs);
	}

	enum class EOperation
	{
		NONE,
		offset,
		follow,
		strcmp,
		wcscmp,
		cmpi8,
		cmpi16,
		cmpi32,
		cmpi64,
		pushaddr,
		popaddr,
		advwcard
	};

	struct Instruction
	{
		EOperation Operation;
		int64_t Value;
		const char* String;
		const wchar_t* WString;

		constexpr Instruction():Operation(EOperation::NONE),Value(0),String(nullptr),WString(nullptr){}
		constexpr Instruction(EOperation aOp):Operation(aOp),Value(0),String(nullptr),WString(nullptr){}
		constexpr Instruction(EOperation aOp,int64_t aValue):Operation(aOp),Value(aValue),String(nullptr),WString(nullptr){}
		constexpr Instruction(EOperation aOp,const char* aStr):Operation(aOp),Value(0),String(aStr),WString(nullptr){}
		constexpr Instruction(EOperation aOp,const wchar_t* aWStr):Operation(aOp),Value(0),String(nullptr),WString(aWStr){}

		inline constexpr Instruction(const Instruction& aOther):Operation(aOther.Operation),Value(aOther.Value),String(aOther.String),WString(aOther.WString){}

		inline Instruction& operator=(const Instruction& aOther)
		{
			if(this==&aOther)
				return *this;

			Operation=aOther.Operation;
			Value=0;
			String=nullptr;
			WString=nullptr;

			switch(Operation)
			{
				case EOperation::strcmp:
					String=aOther.String;
					break;

				case EOperation::wcscmp:
					WString=aOther.WString;
					break;

				default:
					Value=aOther.Value;
					break;
			}

			return *this;
		}
	};
	
	enum class EScanScope
	{
		SpecifiedModule,
		UserModules,
		AllModules
	};

	struct ScanTarget
	{
		EScanScope Scope=EScanScope::UserModules;
		HMODULE ModuleHandle=nullptr;

		constexpr ScanTarget()=default;

		constexpr ScanTarget(EScanScope aScope,HMODULE aModule=nullptr):Scope(aScope),ModuleHandle(aModule){}

		[[nodiscard]] static ScanTarget SpecifiedModule(HMODULE aModule)
		{
			return ScanTarget(EScanScope::SpecifiedModule,aModule);
		}

		[[nodiscard]] static ScanTarget SpecifiedModule(const wchar_t* aModuleName)
		{
			return ScanTarget(EScanScope::SpecifiedModule,aModuleName?GetModuleHandleW(aModuleName):nullptr);
		}

		[[nodiscard]] static ScanTarget SpecifiedModule(const char* aModuleName)
		{
			return ScanTarget(EScanScope::SpecifiedModule,aModuleName?GetModuleHandleA(aModuleName):nullptr);
		}

		[[nodiscard]] static ScanTarget Module(HMODULE aModule){return SpecifiedModule(aModule);}
		[[nodiscard]] static ScanTarget Module(const wchar_t* aModuleName){return SpecifiedModule(aModuleName);}
		[[nodiscard]] static ScanTarget Module(const char* aModuleName){return SpecifiedModule(aModuleName);}
		[[nodiscard]] static ScanTarget CurrentModule(){return SpecifiedModule(GetModuleHandleW(nullptr));}
		[[nodiscard]] static constexpr ScanTarget UserModules(){return ScanTarget(EScanScope::UserModules,nullptr);}
		[[nodiscard]] static constexpr ScanTarget AllModules(){return ScanTarget(EScanScope::AllModules,nullptr);}
	};
	
	namespace Detail
	{
		struct AddressRange
		{
			PBYTE Begin=nullptr;
			PBYTE End=nullptr;
			HMODULE Module=nullptr;
		};

		inline bool IsExecutableProtection(DWORD protection) noexcept
		{
			if (protection & (PAGE_GUARD | PAGE_NOACCESS))
				return false;

			switch (protection & 0xFF)
			{
				case PAGE_EXECUTE:
				case PAGE_EXECUTE_READ:
				case PAGE_EXECUTE_READWRITE:
				case PAGE_EXECUTE_WRITECOPY:
					return true;

				default:
					return false;
			}
		}

		inline bool SafeReadBytes(
			const void* source,
			void* destination,
			std::size_t size) noexcept
		{
			if (!source || !destination || size == 0)
				return false;

			SIZE_T bytesRead = 0;

			return ::ReadProcessMemory(
					   ::GetCurrentProcess(),
					   source,
					   destination,
					   size,
					   &bytesRead) != FALSE
				&& bytesRead == size;
		}

		inline DWORD BaseProtection(DWORD protection) noexcept
		{
			return protection & 0xFF;
		}

		inline bool IsWritableProtection(DWORD protection) noexcept
		{
			switch (BaseProtection(protection))
			{
			case PAGE_READWRITE:
			case PAGE_WRITECOPY:
			case PAGE_EXECUTE_READWRITE:
			case PAGE_EXECUTE_WRITECOPY:
				return true;

			default:
				return false;
			}
		}

		inline LONG MemoryCopyExceptionFilter(DWORD code) noexcept
		{
			switch (code)
			{
			case EXCEPTION_ACCESS_VIOLATION:
			case EXCEPTION_IN_PAGE_ERROR:
			case EXCEPTION_GUARD_PAGE:
				return EXCEPTION_EXECUTE_HANDLER;

			default:
				return EXCEPTION_CONTINUE_SEARCH;
			}
		}

		__declspec(noinline)
		inline bool SehMemmove(
			void* destination,
			const void* source,
			std::size_t size) noexcept
		{
			__try
			{
				std::memmove(destination, source, size);
				return true;
			}
			__except (
				MemoryCopyExceptionFilter(GetExceptionCode()))
			{
				return false;
			}
		}

		inline bool SafeWriteBytes(
			void* destination,
			const void* source,
			std::size_t size) noexcept
		{
			if (!destination || !source || size == 0)
				return false;

			MEMORY_BASIC_INFORMATION mbi{};

			if (::VirtualQuery(
					destination,
					&mbi,
					sizeof(mbi)) == 0)
			{
				return false;
			}

			if (mbi.State != MEM_COMMIT)
				return false;

			if ((mbi.Protect & PAGE_GUARD) != 0)
				return false;

			if (BaseProtection(mbi.Protect) == PAGE_NOACCESS)
				return false;

			const auto address =
				reinterpret_cast<std::uintptr_t>(destination);

			const auto regionBase =
				reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);

			if (address < regionBase)
				return false;

			const auto offset = address - regionBase;

			if (offset > mbi.RegionSize ||
				size > mbi.RegionSize - offset)
			{
				return false;
			}

			const bool executable =
				IsExecutableProtection(mbi.Protect);

			const bool protectionChangeRequired =
				!IsWritableProtection(mbi.Protect);

			DWORD oldProtection = mbi.Protect;

			if (protectionChangeRequired)
			{
				const DWORD temporaryProtection =
					executable
						? PAGE_EXECUTE_READWRITE
						: PAGE_READWRITE;

				if (!::VirtualProtect(
						destination,
						size,
						temporaryProtection,
						&oldProtection))
				{
					return false;
				}
			}

			const bool copySucceeded =
				SehMemmove(destination, source, size);

			bool restoreSucceeded = true;

			if (protectionChangeRequired)
			{
				DWORD ignoredProtection{};

				restoreSucceeded =
					::VirtualProtect(
						destination,
						size,
						oldProtection,
						&ignoredProtection) != FALSE;
			}

			bool flushSucceeded = true;

			if (copySucceeded && executable)
			{
				flushSucceeded =
					::FlushInstructionCache(
						::GetCurrentProcess(),
						destination,
						size) != FALSE;
			}

			return copySucceeded &&
				   restoreSucceeded &&
				   flushSucceeded;
		}

		template<typename T>
		inline bool SafeWrite(
			void* destination,
			const T& value) noexcept
		{
			static_assert(
				std::is_trivially_copyable_v<T>);

			return SafeWriteBytes(
				destination,
				std::addressof(value),
				sizeof(T));
		}

		template<typename T>
		inline bool SafeRead(const void* source, T& value) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);

			return SafeReadBytes(
				source,
				std::addressof(value),
				sizeof(T));
		}

		inline bool IsPathInsideDirectory(const std::wstring& aPath,const std::wstring& aDirectory)
		{
			if(aDirectory.empty()||aPath.size()<aDirectory.size())
				return false;

			int result=CompareStringOrdinal(aPath.data(),static_cast<int>(aDirectory.size()),aDirectory.data(),static_cast<int>(aDirectory.size()),TRUE);

			if(result!=CSTR_EQUAL)
				return false;

			if(aPath.size()==aDirectory.size())
				return true;

			wchar_t separator=aPath[aDirectory.size()];
			return separator==L'\\'||separator==L'/';
		}

		inline std::wstring GetWindowsDirectoryPath()
		{
			std::wstring path(32768,L'\0');
			UINT length=GetWindowsDirectoryW(path.data(),static_cast<UINT>(path.size()));

			if(length==0||length>=path.size())
				return {};

			path.resize(length);

			while(!path.empty()&&(path.back()==L'\\'||path.back()==L'/'))
				path.pop_back();

			return path;
		}

		inline std::wstring GetModulePath(HMODULE aModule)
		{
			std::wstring path(32768,L'\0');
			DWORD length=GetModuleFileNameW(aModule,path.data(),static_cast<DWORD>(path.size()));

			if(length==0||length>=path.size())
				return {};

			path.resize(length);
			return path;
		}

		inline bool IsUserModule(HMODULE aModule,const std::wstring& aWindowsDirectory)
		{
			if(aModule==GetModuleHandleW(nullptr))
				return true;

			std::wstring modulePath=GetModulePath(aModule);

			if(modulePath.empty())
				return true;

			return !IsPathInsideDirectory(modulePath,aWindowsDirectory);
		}

		inline std::vector<HMODULE> EnumerateProcessModules()
		{
			std::vector<HMODULE> modules(128);
			DWORD bytesNeeded=0;

			for(;;)
			{
				DWORD bufferSize=static_cast<DWORD>(modules.size()*sizeof(HMODULE));

				if(!EnumProcessModulesEx(GetCurrentProcess(),modules.data(),bufferSize,&bytesNeeded,LIST_MODULES_ALL))
					throw std::runtime_error("Failed to enumerate process modules.");

				if(bytesNeeded<=bufferSize)
				{
					modules.resize(bytesNeeded/sizeof(HMODULE));
					return modules;
				}

				modules.resize(bytesNeeded/sizeof(HMODULE)+16);
			}
		}

		inline AddressRange GetModuleRange(HMODULE aModule)
		{
			MODULEINFO moduleInfo{};

			if(!aModule||!GetModuleInformation(GetCurrentProcess(),aModule,&moduleInfo,sizeof(moduleInfo)))
				throw std::runtime_error("Failed to query module information.");

			PBYTE begin=static_cast<PBYTE>(moduleInfo.lpBaseOfDll);
			return AddressRange{begin,begin+moduleInfo.SizeOfImage,aModule};
		}

		inline std::vector<AddressRange> GetSearchRanges(const ScanTarget& aTarget)
		{
			if(aTarget.Scope==EScanScope::SpecifiedModule)
			{
				if(!aTarget.ModuleHandle)
					throw std::invalid_argument("SpecifiedModule requires a valid HMODULE.");

				return {GetModuleRange(aTarget.ModuleHandle)};
			}

			std::vector<HMODULE> modules=EnumerateProcessModules();
			std::wstring windowsDirectory=GetWindowsDirectoryPath();
			std::vector<AddressRange> ranges;
			ranges.reserve(modules.size());

			for(HMODULE module:modules)
			{
				if(aTarget.Scope==EScanScope::UserModules&&!IsUserModule(module,windowsDirectory))
					continue;

				try
				{
					ranges.push_back(GetModuleRange(module));
				}
				catch(const std::runtime_error&)
				{
				}
			}

			std::sort(ranges.begin(),ranges.end(),[](const AddressRange& left,const AddressRange& right)
			{
				return left.Begin<right.Begin;
			});

			ranges.erase(std::unique(ranges.begin(),ranges.end(),[](const AddressRange& left,const AddressRange& right)
			{
				return left.Module==right.Module;
			}),ranges.end());

			return ranges;
		}

		inline bool ContainsAddress(const AddressRange& range,const void* addressValue,std::size_t size)
		{
			auto begin=reinterpret_cast<std::uintptr_t>(range.Begin);
			auto end=reinterpret_cast<std::uintptr_t>(range.End);
			auto address=reinterpret_cast<std::uintptr_t>(addressValue);

			if(address<begin||address>=end)
				return false;

			return size<=end-address;
		}

		inline PBYTE FindPattern(PBYTE begin,std::size_t size,const Pattern& pattern)
		{
			if(!begin||pattern.Size==0||pattern.Size>size)
				return nullptr;

			std::size_t anchorIndex=0;

			while(anchorIndex<pattern.Size&&pattern.Bytes[anchorIndex].IsWildcard)
				anchorIndex++;

			if(anchorIndex==pattern.Size)
				return begin;

			std::size_t patternSize=static_cast<std::size_t>(pattern.Size);
			std::size_t lastStart=size-patternSize;
			PBYTE search=begin+anchorIndex;
			PBYTE searchEnd=begin+lastStart+anchorIndex+1;
			uint8_t anchorValue=pattern.Bytes[anchorIndex].Value;

			while(search<searchEnd)
			{
				std::size_t remaining=static_cast<std::size_t>(searchEnd-search);
				PBYTE found=static_cast<PBYTE>(std::memchr(search,anchorValue,remaining));

				if(!found)
					return nullptr;

				PBYTE candidate=found-anchorIndex;

				if(pattern==candidate)
					return candidate;

				search=found+1;
			}

			return nullptr;
		}

		inline std::uint64_t HashBytes(std::uint64_t hash,const void* data,std::size_t size)
		{
			constexpr std::uint64_t prime=1099511628211ull;
			const auto* bytes=static_cast<const std::uint8_t*>(data);

			for(std::size_t i=0;i<size;i++)
			{
				hash^=bytes[i];
				hash*=prime;
			}

			return hash;
		}

		inline std::uint64_t HashString(std::uint64_t hash,const char* string)
		{
			if(!string)
			{
				const std::uint8_t empty=0;
				return HashBytes(hash,&empty,sizeof(empty));
			}

			return HashBytes(hash,string,std::strlen(string)+1);
		}

		inline std::uint64_t HashWideString(std::uint64_t hash,const wchar_t* string)
		{
			if(!string)
			{
				const wchar_t empty=L'\0';
				return HashBytes(hash,&empty,sizeof(empty));
			}

			return HashBytes(hash,string,(std::wcslen(string)+1)*sizeof(wchar_t));
		}
	}
	
	inline void* FollowRelativeAddress(void* aAddress)
	{
		if(!aAddress)
			return nullptr;

		int32_t jmpOffset{};

		if(!Detail::SafeRead(aAddress,jmpOffset))
			return nullptr;

		return (PBYTE)aAddress+jmpOffset+4;
	}

	inline void* FollowJmpChain(PBYTE pointer)
	{
		if(!pointer)
			return nullptr;

		for(std::size_t depth = 0; depth < 64; ++depth)
		{
			std::uint8_t opcode{};

			if(!Detail::SafeRead(pointer, opcode))
				return nullptr;

			if(opcode == 0xEB)
			{
				std::int8_t offset{};

				if(!Detail::SafeRead(pointer + 1, offset))
					return nullptr;

				pointer += 2 + offset;
			}
			else if(opcode == 0xE9)
			{
				std::int32_t offset{};

				if(!Detail::SafeRead(pointer + 1, offset))
					return nullptr;

				pointer += 5 + offset;
			}
			else if(opcode == 0xFF)
			{
				std::uint8_t secondOpcode{};

				if(!Detail::SafeRead(pointer + 1, secondOpcode))
					return nullptr;

				if(secondOpcode != 0x25)
					break;

#ifdef _WIN64
				std::int32_t offset{};

				if(!Detail::SafeRead(pointer + 2, offset))
					return nullptr;

				PBYTE indirectAddress = pointer + 6 + offset;

				if(!Detail::SafeRead(indirectAddress, pointer))
					return nullptr;
#else
				std::uint32_t indirectAddress{};

				if(!Detail::SafeRead(pointer + 2, indirectAddress))
					return nullptr;

				if(!Detail::SafeRead(
					reinterpret_cast<const void*>(
						static_cast<std::uintptr_t>(indirectAddress)),
					pointer))
				{
					return nullptr;
				}
#endif
			}
			else
			{
				return pointer;
			}
		}

		return pointer;
	}

	constexpr Instruction Offset(int64_t aValue){return Instruction(EOperation::offset,aValue);}
	constexpr Instruction Follow(){return Instruction(EOperation::follow);}
	constexpr Instruction Strcmp(const char* aStr){return Instruction(EOperation::strcmp,aStr);}
	constexpr Instruction Wcscmp(const wchar_t* aWStr){return Instruction(EOperation::wcscmp,aWStr);}
	constexpr Instruction CmpI8(int64_t aValue){return Instruction(EOperation::cmpi8,aValue);}
	constexpr Instruction CmpI16(int64_t aValue){return Instruction(EOperation::cmpi16,aValue);}
	constexpr Instruction CmpI32(int64_t aValue){return Instruction(EOperation::cmpi32,aValue);}
	constexpr Instruction CmpI64(int64_t aValue){return Instruction(EOperation::cmpi64,aValue);}
	constexpr Instruction PushAddr(){return Instruction(EOperation::pushaddr);}
	constexpr Instruction PopAddr(){return Instruction(EOperation::popaddr);}
	constexpr Instruction AdvWcard(int64_t aSets=1){return Instruction(EOperation::advwcard,aSets>1?aSets:1);}

	using ScanOptions=ScanTarget;

	template<typename T>
	inline bool WriteValue(
		std::uintptr_t address,
		const T& value) noexcept
	{
		static_assert(
			std::is_trivially_copyable_v<T>);

		if (address == 0)
			return false;

		return Detail::SafeWrite(
			reinterpret_cast<void*>(address),
			value);
	}


	template<typename T>
	inline T ReadValue(std::uintptr_t address)
	{
		if(!address)
			throw std::invalid_argument("ReadValue received null address.");


		T value{};

		if(!Detail::SafeRead(
			reinterpret_cast<const void*>(address),
			value))
		{
			throw std::runtime_error(
				"ReadValue failed.");
		}

		return value;
	}

	inline uintptr_t GetModuleBase(const wchar_t* moduleName = nullptr)
	{
		HMODULE hModule = GetModuleHandleW(moduleName);
		return hModule ? reinterpret_cast<uintptr_t>(hModule) : 0;
	}

	inline uintptr_t GetModuleBase(const char* moduleName)
	{
		HMODULE hModule = GetModuleHandleA(moduleName);
		return hModule ? reinterpret_cast<uintptr_t>(hModule) : 0;
	}

	[[nodiscard]] inline std::uintptr_t CalculatePointerChain(HMODULE module,std::uintptr_t baseOffset,std::span<const std::uintptr_t> offsets)
	{
		if(!module)
			throw std::invalid_argument("CalculatePointerChain received a null module.");

		std::uintptr_t address=reinterpret_cast<std::uintptr_t>(module);

		if(baseOffset>std::numeric_limits<std::uintptr_t>::max()-address)
			throw std::overflow_error("Pointer chain base address overflow.");

		address+=baseOffset;

		for(std::uintptr_t offset:offsets)
		{
			if(!address)
				throw std::runtime_error("Null address encountered in pointer chain.");

			std::uintptr_t next=0;

			if(!Detail::SafeRead(reinterpret_cast<const void*>(address),next))
				throw std::runtime_error("Failed to read pointer in pointer chain.");

			if(!next)
				throw std::runtime_error("Null pointer encountered in pointer chain.");

			if(offset>std::numeric_limits<std::uintptr_t>::max()-next)
				throw std::overflow_error("Pointer chain address overflow.");

			address=next+offset;
		}

		return address;
	}

	[[nodiscard]] inline std::uintptr_t CalculatePointerChain(std::uintptr_t baseOffset,std::span<const std::uintptr_t> offsets,const wchar_t* moduleName=nullptr)
	{
		HMODULE module=GetModuleHandleW(moduleName);

		if(!module)
			throw std::runtime_error("Module was not found.");

		return CalculatePointerChain(module,baseOffset,offsets);
	}

	[[nodiscard]] inline std::uintptr_t CalculatePointerChain(std::uintptr_t baseOffset,std::span<const std::uintptr_t> offsets,const char* moduleName)
	{
		HMODULE module=GetModuleHandleA(moduleName);

		if(!module)
			throw std::runtime_error("Module was not found.");

		return CalculatePointerChain(module,baseOffset,offsets);
	}

	[[nodiscard]] inline std::uintptr_t CalculatePointerChain(HMODULE module,std::uintptr_t baseOffset,std::initializer_list<std::uintptr_t> offsets)
	{
		return CalculatePointerChain(module,baseOffset,std::span<const std::uintptr_t>(offsets.begin(),offsets.size()));
	}

	[[nodiscard]] inline std::uintptr_t CalculatePointerChain(std::uintptr_t baseOffset,std::initializer_list<std::uintptr_t> offsets,const wchar_t* moduleName=nullptr)
	{
		return CalculatePointerChain(baseOffset,std::span<const std::uintptr_t>(offsets.begin(),offsets.size()),moduleName);
	}

	[[nodiscard]] inline std::uintptr_t CalculatePointerChain(std::uintptr_t baseOffset,std::initializer_list<std::uintptr_t> offsets,const char* moduleName)
	{
		return CalculatePointerChain(baseOffset,std::span<const std::uintptr_t>(offsets.begin(),offsets.size()),moduleName);
	}

	struct PatternScan
	{
		Pattern Assembly;
		std::array<Instruction, MaxInstructionLength> Instructions = {};
		std::size_t Count=0;

		template<typename... Instrs>
		constexpr PatternScan(Pattern aASM,Instrs... aInstructions):Assembly(aASM),Instructions{aInstructions...},Count(sizeof...(Instrs))
		{
			static_assert(
				sizeof...(Instrs) <= MaxInstructionLength,
				"Too many instructions for PatternScan."
			);
			static_assert((std::is_same_v<Instrs,Instruction>&&...),"PatternScan arguments must be Instruction values.");
		}

	private:
		[[nodiscard]] std::uint64_t Fingerprint() const
		{
			std::uint64_t hash=1469598103934665603ull;
			hash=Detail::HashBytes(hash,&Count,sizeof(Count));

			for(std::size_t i=0;i<Count;i++)
			{
				const Instruction& instruction=Instructions[i];
				hash=Detail::HashBytes(hash,&instruction.Operation,sizeof(instruction.Operation));

				switch(instruction.Operation)
				{
					case EOperation::strcmp:
						hash=Detail::HashString(hash,instruction.String);
						break;

					case EOperation::wcscmp:
						hash=Detail::HashWideString(hash,instruction.WString);
						break;

					default:
						hash=Detail::HashBytes(hash,&instruction.Value,sizeof(instruction.Value));
						break;
				}
			}

			return hash;
		}

		[[nodiscard]] void* ApplyInstructions(PBYTE matchAddress) const
		{
			if(!matchAddress)
				return nullptr;

			void* resultAddress=matchAddress;
			std::vector<void*> addressStore;
			int64_t offsetFromMatch=0;

			for(std::size_t instructionIndex=0;instructionIndex<Count;instructionIndex++)
			{
				const Instruction& instruction=Instructions[instructionIndex];

				switch(instruction.Operation)
				{
					case EOperation::offset:
						offsetFromMatch+=instruction.Value;
						resultAddress=static_cast<PBYTE>(resultAddress)+instruction.Value;
						break;

					case EOperation::follow:
					{
						if(!resultAddress)
							return nullptr;

						void* followed=FollowRelativeAddress(resultAddress);

						if(!followed)
							return nullptr;

						resultAddress=followed;
						break;
					}

					case EOperation::strcmp:
					{
						if(!instruction.String||!resultAddress)
							return nullptr;

						const auto* stringAddress=static_cast<const char*>(FollowRelativeAddress(resultAddress));

						if(!stringAddress)
							return nullptr;

						std::size_t maxLen=std::strlen(instruction.String)+1;
						std::vector<char> buffer(maxLen);

						if(!Detail::SafeReadBytes(stringAddress,buffer.data(),maxLen))
							return nullptr;

						buffer.back()='\0';

						if(std::strcmp(buffer.data(),instruction.String)!=0)
							return nullptr;

						break;
					}

					case EOperation::wcscmp:
					{
						if(!instruction.WString||!resultAddress)
							return nullptr;

						const auto* stringAddress=static_cast<const wchar_t*>(FollowRelativeAddress(resultAddress));

						if(!stringAddress)
							return nullptr;

						std::size_t maxLen=std::wcslen(instruction.WString)+1;
						std::vector<wchar_t> buffer(maxLen);

						if(!Detail::SafeReadBytes(stringAddress,buffer.data(),maxLen*sizeof(wchar_t)))
							return nullptr;

						buffer.back()=L'\0';

						if(std::wcscmp(buffer.data(),instruction.WString)!=0)
							return nullptr;

						break;
					}

					case EOperation::cmpi8:
					{
						if(!resultAddress)
							return nullptr;

						int8_t value{};

						if(!Detail::SafeRead(resultAddress,value))
							return nullptr;

						if(value!=static_cast<int8_t>(instruction.Value))
							return nullptr;

						break;
					}

					case EOperation::cmpi16:
					{
						if(!resultAddress)
							return nullptr;

						int16_t value{};

						if(!Detail::SafeRead(resultAddress,value))
							return nullptr;

						if(value!=static_cast<int16_t>(instruction.Value))
							return nullptr;

						break;
					}

					case EOperation::cmpi32:
					{
						if(!resultAddress)
							return nullptr;

						int32_t value{};

						if(!Detail::SafeRead(resultAddress,value))
							return nullptr;

						if(value!=static_cast<int32_t>(instruction.Value))
							return nullptr;

						break;
					}

					case EOperation::cmpi64:
					{
						if(!resultAddress)
							return nullptr;

						int64_t value{};

						if(!Detail::SafeRead(resultAddress,value))
							return nullptr;

						if(value!=static_cast<int64_t>(instruction.Value))
							return nullptr;

						break;
					}

					case EOperation::pushaddr:
						addressStore.push_back(resultAddress);
						break;

					case EOperation::popaddr:
						if(addressStore.empty())
							return nullptr;

						resultAddress=addressStore.back();
						addressStore.pop_back();
						break;

					case EOperation::advwcard:
					{
						for(int64_t setIndex = 0; setIndex < instruction.Value; setIndex++)
						{
							if(offsetFromMatch < 0 || offsetFromMatch >= static_cast<int64_t>(Assembly.Size))
								return nullptr;

							while(offsetFromMatch < static_cast<int64_t>(Assembly.Size)
								  && !Assembly.Bytes[offsetFromMatch].IsWildcard)
							{
								offsetFromMatch++;
							}

							if(offsetFromMatch >= static_cast<int64_t>(Assembly.Size))
								return nullptr;

							while(offsetFromMatch < static_cast<int64_t>(Assembly.Size)
								  && Assembly.Bytes[offsetFromMatch].IsWildcard)
							{
								offsetFromMatch++;
							}
						}

						if(offsetFromMatch < 0 || offsetFromMatch > static_cast<int64_t>(Assembly.Size))
							return nullptr;

						resultAddress = matchAddress + offsetFromMatch;
						break;
					}

					case EOperation::NONE:
					default:
						break;
				}
			}

			return resultAddress;
		}

	public:
		[[nodiscard]] inline void* ScanRaw(ScanTarget target=ScanTarget::UserModules()) const
		{
			if(Assembly.Size==0)
				return nullptr;

			if(target.Scope==EScanScope::SpecifiedModule&&!target.ModuleHandle)
				throw std::invalid_argument("SpecifiedModule requires a loaded module.");

			std::vector<Detail::AddressRange> searchRanges=Detail::GetSearchRanges(target);

			if(searchRanges.empty())
				return nullptr;

			struct CachedMatch
			{
				Pattern PatternValue;
				std::uint64_t InstructionFingerprint=0;
				EScanScope Scope=EScanScope::UserModules;
				HMODULE ModuleHandle=nullptr;
				PBYTE Address=nullptr;
			};

			static std::mutex cacheMutex;
			static std::vector<CachedMatch> cache;

			std::uint64_t fingerprint=Fingerprint();

			{
				std::lock_guard<std::mutex> lock(cacheMutex);

				auto cacheIterator=std::find_if(cache.begin(),cache.end(),[this,fingerprint,target](const CachedMatch& match)
				{
					return match.PatternValue==Assembly&&match.InstructionFingerprint==fingerprint&&match.Scope==target.Scope&&match.ModuleHandle==target.ModuleHandle;
				});

				if(cacheIterator!=cache.end())
				{
					bool cacheValid=false;

					for(const Detail::AddressRange& range:searchRanges)
					{
						if(!Detail::ContainsAddress(range,cacheIterator->Address,static_cast<std::size_t>(Assembly.Size)))
							continue;

						MEMORY_BASIC_INFORMATION memoryInfo{};

						if(VirtualQuery(cacheIterator->Address,&memoryInfo,sizeof(memoryInfo))==0)
							break;

						auto regionEnd=reinterpret_cast<std::uintptr_t>(memoryInfo.BaseAddress)+memoryInfo.RegionSize;
						auto matchEnd=reinterpret_cast<std::uintptr_t>(cacheIterator->Address)+static_cast<std::size_t>(Assembly.Size);

						cacheValid=memoryInfo.State==MEM_COMMIT&&Detail::IsExecutableProtection(memoryInfo.Protect)&&matchEnd<=regionEnd&&Assembly==cacheIterator->Address;
						break;
					}

					if(cacheValid)
					{
						if(void* result=ApplyInstructions(cacheIterator->Address))
							return result;
					}

					cache.erase(cacheIterator);
				}
			}

			for(const Detail::AddressRange& moduleRange:searchRanges)
			{
				PBYTE cursor=moduleRange.Begin;

				while(cursor<moduleRange.End)
				{
					MEMORY_BASIC_INFORMATION memoryInfo{};

					if(VirtualQuery(cursor,&memoryInfo,sizeof(memoryInfo))==0)
						break;

					PBYTE virtualBegin=static_cast<PBYTE>(memoryInfo.BaseAddress);
					PBYTE virtualEnd=virtualBegin+memoryInfo.RegionSize;

					if(virtualEnd<=cursor)
						break;

					cursor=virtualEnd;

					if(memoryInfo.State!=MEM_COMMIT||!Detail::IsExecutableProtection(memoryInfo.Protect))
						continue;

					PBYTE scanBegin=(std::max)(virtualBegin,moduleRange.Begin);
					PBYTE scanEnd=(std::min)(virtualEnd,moduleRange.End);

					if(scanEnd<=scanBegin)
						continue;

					PBYTE searchBegin=scanBegin;

					while(searchBegin<scanEnd)
					{
						std::size_t remaining=static_cast<std::size_t>(scanEnd-searchBegin);
						PBYTE matchAddress=Detail::FindPattern(searchBegin,remaining,Assembly);

						if(!matchAddress)
							break;

						if(void* result=ApplyInstructions(matchAddress))
						{
							std::lock_guard<std::mutex> lock(cacheMutex);

							auto oldMatch=std::find_if(cache.begin(),cache.end(),[this,fingerprint,target](const CachedMatch& match)
							{
								return match.PatternValue==Assembly&&match.InstructionFingerprint==fingerprint&&match.Scope==target.Scope&&match.ModuleHandle==target.ModuleHandle;
							});

							if(oldMatch==cache.end())
								cache.push_back(CachedMatch{Assembly,fingerprint,target.Scope,target.ModuleHandle,matchAddress});
							else
								oldMatch->Address=matchAddress;

							return result;
						}

						searchBegin=matchAddress+1;
					}
				}
			}

			return nullptr;
		}

		template<typename T=void*>
		[[nodiscard]] inline T Scan(ScanTarget target=ScanTarget::UserModules()) const
		{
			return reinterpret_cast<T>(ScanRaw(target));
		}

		template<typename T=void*>
		[[nodiscard]] inline T Scan(EScanScope scope,HMODULE module=nullptr) const
		{
			return Scan<T>(ScanTarget(scope,module));
		}

		template<typename T=void*>
		[[nodiscard]] inline T Scan(HMODULE module) const
		{
			return Scan<T>(ScanTarget::SpecifiedModule(module));
		}

		template<typename T=void*>
		[[nodiscard]] inline T Scan(const wchar_t* moduleName) const
		{
			return Scan<T>(ScanTarget::SpecifiedModule(moduleName));
		}

		template<typename T=void*>
		[[nodiscard]] inline T Scan(const char* moduleName) const
		{
			return Scan<T>(ScanTarget::SpecifiedModule(moduleName));
		}
	};

	struct FallbackScan
	{
		std::vector<PatternScan> Scans;

		inline FallbackScan(std::initializer_list<PatternScan> scans)
		{
			for(const PatternScan& scan:scans)
				Scans.push_back(scan);
		}

		template<typename T=void*>
		[[nodiscard]] inline T Scan(ScanTarget target=ScanTarget::UserModules()) const
		{
			for(const PatternScan& scan:Scans)
			{
				void* result=scan.Scan<void*>(target);

				if(result)
					return reinterpret_cast<T>(result);
			}

			return T{};
		}

		template<typename T=void*>
		[[nodiscard]] inline T Scan(EScanScope scope,HMODULE module=nullptr) const
		{
			return Scan<T>(ScanTarget(scope,module));
		}

		template<typename T=void*>
		[[nodiscard]] inline T Scan(HMODULE module) const
		{
			return Scan<T>(ScanTarget::SpecifiedModule(module));
		}

		template<typename T=void*>
		[[nodiscard]] inline T Scan(const wchar_t* moduleName) const
		{
			return Scan<T>(ScanTarget::SpecifiedModule(moduleName));
		}

		template<typename T=void*>
		[[nodiscard]] inline T Scan(const char* moduleName) const
		{
			return Scan<T>(ScanTarget::SpecifiedModule(moduleName));
		}
	};

	class Patch final
	{
	public:
		Patch(void* target,std::span<const std::uint8_t> replacement,bool enableImmediately=true):target_(target),replacement_(replacement.begin(),replacement.end()),original_(replacement.size())
		{
			if(!target_)
				throw std::invalid_argument("Patch target is null.");

			if(replacement_.empty())
				throw std::invalid_argument("Patch bytes are empty.");

			MEMORY_BASIC_INFORMATION mbi{};

			if(VirtualQuery(target_,&mbi,sizeof(mbi))==0||mbi.State!=MEM_COMMIT)
				throw std::runtime_error("Patch target is not committed memory.");

			auto targetAddress=reinterpret_cast<std::uintptr_t>(target_);
			auto regionEnd=reinterpret_cast<std::uintptr_t>(mbi.BaseAddress)+mbi.RegionSize;

			if(targetAddress>regionEnd||original_.size()>regionEnd-targetAddress)
				throw std::runtime_error("Patch crosses memory region boundary.");

			if (!Detail::SafeReadBytes(
					target_,
					original_.data(),
					original_.size()))
			{
				throw std::runtime_error(
					"Failed to read original patch bytes.");
			}

			if(enableImmediately&&!Enable())
				throw std::runtime_error("Failed to enable patch.");
		}

		Patch(void* target,std::initializer_list<std::uint8_t> replacement,bool enableImmediately=true):Patch(target,std::span<const std::uint8_t>(replacement.begin(),replacement.size()),enableImmediately){}

		~Patch() noexcept
		{
			(void)Disable();
		}

		Patch(const Patch&)=delete;
		Patch& operator=(const Patch&)=delete;
		Patch(Patch&&)=delete;
		Patch& operator=(Patch&&)=delete;

		[[nodiscard]] bool Enable() noexcept
		{
			if(enabled_)
				return true;

			if(!Write(replacement_))
				return false;

			enabled_=true;
			return true;
		}

		[[nodiscard]] bool Disable() noexcept
		{
			if(!enabled_)
				return true;

			if(!Write(original_))
				return false;

			enabled_=false;
			return true;
		}

		[[nodiscard]] bool IsEnabled() const noexcept{return enabled_;}
		[[nodiscard]] void* Target() const noexcept{return target_;}
		[[nodiscard]] std::size_t Size() const noexcept{return replacement_.size();}

	private:
		[[nodiscard]] bool Write(const std::vector<uint8_t>& bytes)
			{
				return Detail::SafeWriteBytes(
					target_,
					bytes.data(),
					bytes.size());
			}

		void* target_{};
		std::vector<std::uint8_t> replacement_;
		std::vector<std::uint8_t> original_;
		bool enabled_{};
	};
	
	class PatchManager final
	{
	public:
		PatchManager() = default;
		~PatchManager() noexcept = default;

		PatchManager(const PatchManager&) = delete;
		PatchManager& operator=(const PatchManager&) = delete;
		PatchManager(PatchManager&&) = delete;
		PatchManager& operator=(PatchManager&&) = delete;

		bool Add(const std::string& name, void* target,
				 std::span<const std::uint8_t> replacement,
				 bool enableImmediately = true)
		{
			std::lock_guard<std::mutex> lock(mutex_);

			if(patches_.count(name))
				return false;

			try
			{
				patches_.emplace(name,
					std::make_unique<Patch>(target, replacement, enableImmediately));
				return true;
			}
			catch(const std::exception&)
			{
				return false;
			}
		}

		bool Add(const std::string& name, void* target,
				 std::initializer_list<std::uint8_t> replacement,
				 bool enableImmediately = true)
		{
			return Add(name, target,
				std::span<const std::uint8_t>(replacement.begin(), replacement.size()),
				enableImmediately);
		}

		bool Add(const std::string& name,
				 void* target,
				 const std::string& hex,
				 bool enableImmediately = true)
		{
			std::vector<std::uint8_t> bytes;

			std::stringstream ss(hex);
			std::string byteString;

			while (ss >> byteString)
			{
				if (byteString.size() != 2)
					return false;

				try
				{
					std::size_t consumed = 0;

					const unsigned long parsedValue =
						std::stoul(byteString, &consumed, 16);

					if (consumed != byteString.size() || parsedValue > 0xFF)
						return false;

					bytes.push_back(
						static_cast<std::uint8_t>(parsedValue)
					);
				}
				catch (const std::exception&)
				{
					return false;
				}
			}


			return Add(
				name,
				target,
				std::span<const std::uint8_t>(
					bytes.data(),
					bytes.size()
				),
				enableImmediately
			);
		}

		bool Add(const std::string& name, std::uintptr_t target,
				 std::span<const std::uint8_t> replacement,
				 bool enableImmediately = true)
		{
			return Add(name, reinterpret_cast<void*>(target), replacement, enableImmediately);
		}

		bool Add(const std::string& name, std::uintptr_t target,
				 std::initializer_list<std::uint8_t> replacement,
				 bool enableImmediately = true)
		{
			return Add(name, reinterpret_cast<void*>(target), replacement, enableImmediately);
		}

		bool Add(const std::string& name, std::uintptr_t target,
				 const std::string& hex,
				 bool enableImmediately = true)
		{
			return Add(name, reinterpret_cast<void*>(target), hex, enableImmediately);
		}

		bool Enable(const std::string& name) noexcept
		{
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = patches_.find(name);
			if(it == patches_.end()) return false;
			return it->second->Enable();
		}

		bool Disable(const std::string& name) noexcept
		{
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = patches_.find(name);
			if(it == patches_.end()) return false;
			return it->second->Disable();
		}

		bool Remove(const std::string& name) noexcept
		{
			std::lock_guard<std::mutex> lock(mutex_);
			return patches_.erase(name) > 0;
		}

		void RemoveAll() noexcept
		{
			std::lock_guard<std::mutex> lock(mutex_);
			patches_.clear();
		}

		[[nodiscard]] bool IsEnabled(const std::string& name) const noexcept
		{
			std::lock_guard<std::mutex> lock(mutex_);
			auto it = patches_.find(name);
			if(it == patches_.end()) return false;
			return it->second->IsEnabled();
		}

		[[nodiscard]] bool Has(const std::string& name) const noexcept
		{
			std::lock_guard<std::mutex> lock(mutex_);
			return patches_.count(name) > 0;
		}

	private:
		mutable std::mutex mutex_;
		std::unordered_map<std::string, std::unique_ptr<Patch>> patches_;
	};
}