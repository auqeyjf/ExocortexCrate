cmake_minimum_required (VERSION 2.6) 

project ( Arnold${ARNOLD_MAJOR_VERSION}ExocortexAlembic ) 

get_filename_component( BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}" PATH )
set( LOCAL_CMAKE_FILE_NAME "${BASE_DIR}/CMakeLists.txt" )

include_directories( ${BASE_DIR}/include )
include_directories( ${BASE_DIR}/src )

file(GLOB_RECURSE Sources ${BASE_DIR}/*.cpp)
file(GLOB_RECURSE Includes ${BASE_DIR}/*.h)
 
SOURCE_GROUP("Source Files" FILES ${Sources}) 
SOURCE_GROUP("Header Files" FILES ${Includes})

setup_precompiled_header( ${BASE_DIR} ${Sources} )


if( WIN32 )
	set( Arnold_BASE_DIR "${LIBRARY_ROOT}/arnold/${EXOCORTEX_ARNOLD_VERSION}/windows" )
	set( Arnold_LIBRARIES ai )
elseif( UNIX ) 
	set( Arnold_BASE_DIR "${LIBRARY_ROOT}/arnold/${EXOCORTEX_ARNOLD_VERSION}/linux" )
	#set( Arnold_LIBRARIES ai )  There are no static linkable libraries for Arnold on linux.
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--version-script=${BASE_DIR}/ArnoldAlembic.versionScript")
else()
	message(FATAL_ERROR "Should not get here")
endif()

MESSAGE( "Arnold_BASE_DIR: ${Arnold_BASE_DIR}" )

include_directories( "${Arnold_BASE_DIR}/include" )
link_directories( "${Arnold_BASE_DIR}/lib" )

add_library( ${PROJECT_NAME} SHARED ${Sources} ${Includes})
TARGET_LINK_LIBRARIES( ${PROJECT_NAME}
	CommonUtils
   ${Arnold_LIBRARIES}
   ${ALL_ALEMBIC_LIBS}
   )
SET_TARGET_PROPERTIES( ${PROJECT_NAME} 
                      PROPERTIES OUTPUT_NAME ExocortexAlembicArnold )

ADD_DEPENDENCIES(${PROJECT_NAME} AlembicAbcGeom)

get_target_property( Install_TARGET ${PROJECT_NAME} LOCATION )

if( ARNOLD_MAJOR_VERSION MATCHES 4 ) 
	SET( Install_DIR ${Exocortex_INSTALL_BASE_DIR}/Softimage/Application/ArnoldAlembicDSO/ )

	copy_file_to_directory( ${Install_TARGET} ${Install_DIR} )	

	SET( Install_DIR ${Exocortex_INSTALL_BASE_DIR}/Softimage2014/Application/ArnoldAlembicDSO/ )

	copy_file_to_directory( ${Install_TARGET} ${Install_DIR} )

endif()

SET( Install_DIR ${Exocortex_INSTALL_BASE_DIR}/Arnold${EXOCORTEX_ARNOLD_VERSION}/ )
copy_file_to_directory( ${Install_TARGET} ${Install_DIR} )
