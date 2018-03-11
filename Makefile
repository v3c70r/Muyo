CXX=g++
FLAGS=-std=c++11 -Wall -pthread -g
HEADERS = VertexBuffer.h
SOURCE=helloVulkan.cpp
REF_SOURCE=helloVulkan.cpp
SHADER_CC = ~/tools/VulkanSDK/1.0.68.0/x86_64/bin/glslangValidator

VERT_SRC = triangle.vert
VERT_BIN = vert.spv
FRAG_SRC = triangle.frag
FRAG_BIN = frag.spv

# Set your own SDK file path
SDK_PATH=/home/tsing/tools/VulkanSDK/1.0.24.0

INCLUDES=-I$(SDK_PATH)/x86_64/include -I imgui
LIBS=$(shell pkg-config --static --libs glfw3) -L$(SDK_PATH)/x86_64/lib  -lvulkan

run.sh: helloVulkan $(FRAG_BIN) $(VERT_BIN)
	@echo "LD_LIBRARY_PATH=$(SDK_PATH)/x86_64/lib VK_LAYER_PATH=$(SDK_PATH)/x86_64/etc/explicit_layer.d ./$<" > $@
helloVulkan: $(SOURCE) $(HEADERS)
	$(CXX) $(FLAGS) $(INCLUDES) -o $@ $(SOURCE)  $(LIBS)

$(FRAG_BIN): $(FRAG_SRC)
	$(SHADER_CC) -V $<

$(VERT_BIN): $(VERT_SRC)
	$(SHADER_CC) -V $<

