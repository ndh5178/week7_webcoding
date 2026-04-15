CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
BIN_DIR = bin
DATA_DIR = data
CSV_PATH = $(DATA_DIR)/players_1000000.csv

all: $(BIN_DIR) datagen bench test demo_query query_server

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(DATA_DIR):
	mkdir -p $(DATA_DIR)

datagen: $(BIN_DIR)
	$(CC) $(CFLAGS) \
		src/common/dataset_io.c \
		src/benchmark/datagen.c \
		-o $(BIN_DIR)/datagen

bench: $(BIN_DIR)
	$(CC) $(CFLAGS) \
		src/common/dataset_io.c \
		src/linear/linear_search.c \
		src/btree/btree.c src/bplus_tree/bplus_tree.c \
		src/benchmark/benchmark.c \
		-o $(BIN_DIR)/bench

test: $(BIN_DIR)
	$(CC) $(CFLAGS) \
		src/btree/btree.c src/bplus_tree/bplus_tree.c \
		tests/test_trees.c \
		-o $(BIN_DIR)/test_trees

demo_query: $(BIN_DIR)
	$(CC) $(CFLAGS) \
		src/common/dataset_io.c \
		src/linear/linear_search.c \
		src/btree/btree.c src/bplus_tree/bplus_tree.c \
		src/demo/query_demo.c \
		-o $(BIN_DIR)/query_demo

query_server: $(BIN_DIR)
	$(CC) $(CFLAGS) \
		src/common/dataset_io.c \
		src/linear/linear_search.c \
		src/btree/btree.c src/bplus_tree/bplus_tree.c \
		src/demo/query_server.c \
		-o $(BIN_DIR)/query_server

run: bench
	./$(BIN_DIR)/bench $(CSV_PATH)

csv: datagen $(DATA_DIR)
	./$(BIN_DIR)/datagen 1000000 $(CSV_PATH)

webdata: bench csv
	./$(BIN_DIR)/bench $(CSV_PATH) web/assets/results.json

check: demo_query query_server bench test
	./$(BIN_DIR)/test_trees

result: webdata

clean:
	rm -rf $(BIN_DIR)
