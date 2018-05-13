
CXX=g++
FLAGS=-std=c++14 -Wall -pthread -g

# Set your own SDK file path
ifeq ($(shell uname -s), Darwin)
	SDK_PATH=/Users/Tsing/tools/vulkansdk-macos-1.1.73.0/macOS
else
	SDK_PATH=/home/tsing/tools/VulkanSDK/1.1.70.1/x86_64
endif

INCLUDES=-I$(SDK_PATH)/include -Iimgui
LIBS=$(shell pkg-config --static --libs glfw3) -L$(SDK_PATH)/lib  -lvulkan
SHADER_CC = $(SDK_PATH)/bin/glslangValidator

HEADERS = VertexBuffer.h Buffer.h UniformBuffer.h Util.h Texture.hpp
SOURCE=helloVulkan.cpp thirdparty/tiny_obj_loader.cc

VERT_SRC = triangle.vert
VERT_BIN = vert.spv
FRAG_SRC = triangle.frag
FRAG_BIN = frag.spv
run.sh: helloVulkan $(FRAG_BIN) $(VERT_BIN)
	@echo "LD_LIBRARY_PATH=$(SDK_PATH)/x86_64/lib VK_LAYER_PATH=$(SDK_PATH)/x86_64/etc/explicit_layer.d ./$<" > $@
helloVulkan: $(SOURCE) $(HEADERS)
	$(CXX) $(FLAGS) $(INCLUDES) -o $@ $(SOURCE)  $(LIBS)

$(FRAG_BIN): $(FRAG_SRC)
	$(SHADER_CC) -V $<

$(VERT_BIN): $(VERT_SRC)
	$(SHADER_CC) -V $<

.phony: clean
clean:
	rm helloVulkan

