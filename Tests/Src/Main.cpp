#include <cstdio>

#include <string_view>

int CSwapVK(size_t argc, const std::string_view* argv);
int DCompVK(size_t argc, const std::string_view* argv);
int DXGISwapVK(size_t argc, const std::string_view* argv);
int STMS(size_t argc, const std::string_view* argv);

struct TestSpec
{
	std::string_view Name;
	std::string_view Desc;
	int              (*Entrypoint)(size_t argc, const std::string_view* argv);
};

static constexpr TestSpec c_Tests[] {
	{
     .Name       = "CSwapVK",
     .Desc       = "Composition Swapchain using Vulkan",
     .Entrypoint = CSwapVK,
	 },
	{
     .Name       = "DCompVK",
     .Desc       = "DirectComposition using Vulkan",
     .Entrypoint = DCompVK,
	 },
	{
     .Name       = "DXGISwapVK",
     .Desc       = "DXGI SwapChain using Vulkan",
     .Entrypoint = DXGISwapVK,
	 },
	{
     .Name       = "STMS",
     .Desc       = "Single Threaded Multiple Swapchains",
     .Entrypoint = STMS,
	 },
};

int main(int argc, char** argv)
{
	if (argc <= 0)
	{
		puts("Broken command line");
		return 1;
	}
	size_t            argCount        = 1;
	std::string_view* args            = nullptr;
	const TestSpec*   foundTest       = nullptr;
	char*             commandLineCopy = nullptr;
	if (argc == 1)
	{
		std::string_view commandLineView;
		char             commandLine[4097];
		commandLine[4096] = '\0';
		size_t offset1    = 0;
		size_t offset2    = 0;

		do
		{
			printf("Command line: ");
			if (!fgets(commandLine, 4096, stdin))
			{
				puts("Broken command line");
				return 1;
			}

			commandLineView = commandLine;
			{
				size_t start    = commandLineView.find_first_not_of(' ');
				size_t end      = commandLineView.find_last_not_of(" \n\r");
				commandLineView = commandLineView.substr(start, end - start + 1);
			}

			if (commandLineView.empty())
			{
				puts("Missing test\n"
					 "Usage: test testArgs...");
				continue;
			}

			offset1               = commandLineView.find_first_of(' ');
			std::string_view test = commandLineView.substr(0, offset1);
			if (test == "-h" || test == "--help" || test == "/?" || test == "?")
			{
				puts("Usage: exe test testArgs...\n"
					 "Commands:\n"
					 "  '-h'\n"
					 "  '--help'\n"
					 "  '/?'\n"
					 "  '?'       : Show this help information\n"
					 "  '--tests' : Show valid tests\n"
					 "Valid tests:");
				for (auto& spec : c_Tests)
					printf("  '%.*s': %.*s\n", (int) spec.Name.size(), spec.Name.data(), (int) spec.Desc.size(), spec.Desc.data());
				continue;
			}
			if (test == "--tests")
			{
				puts("Valid tests");
				for (auto& spec : c_Tests)
					printf("  '%.*s': %.*s\n", (int) spec.Name.size(), spec.Name.data(), (int) spec.Desc.size(), spec.Desc.data());
				continue;
			}

			for (auto& spec : c_Tests)
			{
				if (test == spec.Name)
				{
					foundTest = &spec;
					break;
				}
			}
			if (!foundTest)
			{
				printf("Test '%.*s' does not exist\n"
					   "Valid tests:\n",
					   (int) test.size(),
					   test.data());
				for (auto& spec : c_Tests)
					printf("  '%.*s': %.*s\n", (int) spec.Name.size(), spec.Name.data(), (int) spec.Desc.size(), spec.Desc.data());
				continue;
			}
			break;
		}
		while (true);

		size_t copySize = 0;
		offset2         = offset1;
		size_t offset3  = offset2;
		while (offset2 < commandLineView.size())
		{
			offset2 = commandLineView.find_first_not_of(' ', offset2);
			if (offset2 >= commandLineView.size())
				break;
			++argCount;
			offset3   = std::min<size_t>(commandLineView.find_first_of(' ', offset2), commandLineView.size());
			copySize += offset3 - offset2;
			offset2   = offset3;
		}
		commandLineCopy = new char[copySize];
		offset3         = 0;

		args         = new std::string_view[argCount];
		args[0]      = argv[0];
		size_t index = 1;
		while (offset1 < commandLineView.size())
		{
			offset1 = commandLineView.find_first_not_of(' ', offset1);
			if (offset1 >= commandLineView.size())
				break;
			offset2 = std::min<size_t>(commandLineView.find_first_of(' ', offset1), commandLineView.size());
			memcpy(commandLineCopy + offset3, commandLineView.data() + offset1, offset2 - offset1);
			args[index++] = std::string_view { commandLineCopy + offset3, commandLineCopy + offset3 + offset2 - offset1 };
			offset3      += offset2 - offset1;
			offset1       = offset2;
		}
	}
	else
	{
		std::string_view test = argv[1];
		if (test == "-h" || test == "--help" || test == "/?" || test == "?")
		{
			puts("Usage: exe test testArgs...\n"
				 "Commands:\n"
				 "  '-h'\n"
				 "  '--help'\n"
				 "  '/?'\n"
				 "  '?'       : Show this help information\n"
				 "  '--tests' : Show valid tests\n"
				 "Valid tests:");
			for (auto& spec : c_Tests)
				printf("  '%.*s': %.*s\n", (int) spec.Name.size(), spec.Name.data(), (int) spec.Desc.size(), spec.Desc.data());
			return 0;
		}
		if (test == "--tests")
		{
			puts("Valid tests");
			for (auto& spec : c_Tests)
				printf("  '%.*s': %.*s\n", (int) spec.Name.size(), spec.Name.data(), (int) spec.Desc.size(), spec.Desc.data());
			return 0;
		}

		for (auto& spec : c_Tests)
		{
			if (test == spec.Name)
			{
				foundTest = &spec;
				break;
			}
		}
		if (!foundTest)
		{
			printf("Test '%.*s' does not exist\n"
				   "Valid tests:\n",
				   (int) test.size(),
				   test.data());
			for (auto& spec : c_Tests)
				printf("  '%.*s': %.*s\n", (int) spec.Name.size(), spec.Name.data(), (int) spec.Desc.size(), spec.Desc.data());
			return 1;
		}

		argCount = argc - 1;
		args     = new std::string_view[argCount];
		args[0]  = argv[0];
		for (size_t i = 2; i < argc; ++i)
			args[i - 1] = argv[i];
	}
	int result = foundTest->Entrypoint(argCount, args);
	delete[] args;
	delete[] commandLineCopy;
	return result;
}