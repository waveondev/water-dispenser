# 1. main 폴더 바로 아래와, 하위 폴더(ble, wifi, mqtt 등) 안의 모든 .c 파일을 자동으로 긁어모읍니다.
file(GLOB_RECURSE SOURCES "*.c")

# 2. 자동으로 찾아낸 SOURCES 리스트를 한 번에 등록합니다.
# (주의: INCLUDE_DIRS 항목에서 쉼표(,)를 제거하고 한 칸씩 띄워서 작성합니다.)
idf_component_register(SRCS "${SOURCES}"
                       INCLUDE_DIRS "." 
                                    "ble" 
                                    "wifi" 
                                    "cli" 
                                    "mqtt" 
                                    "vl53l0x"
                                    "vl53l0x/core/inc"
                                    "vl53l0x/core/src"
                                    "vl53l0x/platform/inc"
)