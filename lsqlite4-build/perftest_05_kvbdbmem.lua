
local sqlite4 = require("lsqlite4")

sqlite4.load_kvstore_plugin("kvbdbmem.dll", "kvbdbmem");

local db = sqlite4.open("file:mytest.db?kv=kvbdbmem")

print("----------> creating table <----------");
db:exec[[
  create table 
    if not exists 
    table06 (
       c_int integer PRIMARY KEY, 
       c_num number, 
       c_datetime text, 
       c_char char(20), 
       c_varchar varchar(20)
     );
]]

print("----------> preparing SQL INSERT <----------");
local insert_stmt = assert( 
   db:prepare(
      "insert into table06 (c_int, c_num, c_datetime, c_char, c_varchar) values (:p_int, :p_num, :p_datetime, :p_char, :p_varchar)"
      ) 
)

local function insert(v_int, v_num, v_datetime, v_char, v_varchar)
  insert_stmt:bind_values(v_int, v_num, v_datetime, v_char, v_varchar)
  insert_stmt:step()
  insert_stmt:reset()
end

print("----------> preparing SQL SELECT <----------");
local select_stmt = assert( db:prepare("SELECT * FROM table06") )

print("----------> preparing SQL SELECT COUNT(*) <----------");
local select_stmt_count = assert( db:prepare("SELECT COUNT(*) AS num_rows FROM table06") )

local function select()
  for row in select_stmt:nrows() do
    print(row.c_int, row.c_num, row.c_datetime, row.c_char, row.c_varchar)
  end
end

local function select_count()
  for row in select_stmt_count:nrows() do
    print("selected num_rows = ", row.num_rows)
  end
end



local num_rows = 1000000;
local num_rows_per_txn = 1000;
local num_txn = num_rows / num_rows_per_txn;
local curr_txn = 0;
local curr_row = 0;
local curr_row_in_txn = 0;

local start_time = os.clock() 

for curr_txn = 1, num_txn, 1
do
   -- db:exec[[BEGIN TRANSACTION;]]
   db.txn_begin(db);
   for curr_row_in_txn = 1, num_rows_per_txn, 1
   do
       curr_row = curr_row + 1
       insert(curr_row, "123.456", "2018-11-13 08:52:56.803", "qazwsx", "edcrfv")
   end
   -- db:exec[[COMMIT;]]
   db.txn_commit_phase_one(db);
   db.txn_commit_phase_two(db);
end

local end_time = os.clock() 
local elapsed_time = end_time - start_time;
local performance = num_rows / elapsed_time;

print("----------> checking count after 3rd row <----------");
select_count()

print(string.format("\n\n\ninserted rows: %d\n", num_rows)) 
print(string.format("elapsed time: %.2f [s]\n", elapsed_time)) 
print(string.format("performance: %f [rows/s]\n", performance)) 




