workspace("Tests")
	common:addConfigs()
	common:addBuildDefines()

	cppdialect("C++latest")
	rtti("Off")
	exceptionhandling("On")
	flags("MultiProcessorCompile")

	startproject("Tests")
	project("Tests")
		location("Tests/")
		warnings("Extra")
		common:outDirs()
		common:debugDir()

		filter("configurations:Dist")
			kind("WindowedApp")
		filter("configurations:not Dist")
			kind("ConsoleApp")
		filter({})

		includedirs({ "%{prj.location}/Src/" })
		files({ "%{prj.location}/Src/**" })
		removefiles({ "*.DS_Store" })

		pkgdeps({ "commonbuild", "backtrace", "glfw", "vulkan-sdk" })

		common:addActions()