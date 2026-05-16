#!/bin/bash
# Integration tests for the SQL server via HTTP
# Usage: ./test_webserver.sh
# Requires wserver to be running on localhost:8003

HOST="localhost"
PORT="8003"
PASS=0
FAIL=0

# Colour codes
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

pass() { echo -e "${GREEN}PASS${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "${RED}FAIL${NC} $1 -- got: '$2'"; FAIL=$((FAIL+1)); }

# Send a URL-encoded query, return the response body (strips HTTP headers)
query() {
    ./wclient "$HOST" "$PORT" "/sql_server.cgi?$1" 2>/dev/null \
        | grep -v "^Header:"
}

# Clean up any leftover .db files before starting
rm -f *.db

echo "=== Integration Tests ==="

# ── CREATE ──────────────────────────────────────────────
R=$(query "CREATE+TABLE+movies+(id+smallint,+title+char(30),+length+int);")
echo "$R" | grep -q "OK" && pass "CREATE TABLE movies" || fail "CREATE TABLE movies" "$R"

R=$(query "CREATE+TABLE+actors+(id+smallint,+name+char(20));")
echo "$R" | grep -q "OK" && pass "CREATE TABLE actors" || fail "CREATE TABLE actors" "$R"

R=$(query "CREATE+TABLE+movies+(id+smallint,+title+char(30),+length+int);")
echo "$R" | grep -q "ERROR" && pass "CREATE duplicate rejected" || fail "CREATE duplicate rejected" "$R"

R=$(query "CREATE+TABLE+bad;")
echo "$R" | grep -q "ERROR" && pass "CREATE malformed rejected" || fail "CREATE malformed rejected" "$R"

# ── INSERT ──────────────────────────────────────────────
R=$(query "INSERT+INTO+movies+VALUES+(2,+%27Lyle,+Lyle,+Crocodile%27,+100);")
echo "$R" | grep -q "OK" && pass "INSERT row 1" || fail "INSERT row 1" "$R"

R=$(query "INSERT+INTO+movies+VALUES+(3,+%27Wicked%27,+150);")
echo "$R" | grep -q "OK" && pass "INSERT row 2" || fail "INSERT row 2" "$R"

R=$(query "INSERT+INTO+movies+VALUES+(5,+%27A+Complete+Unknown%27,+230);")
echo "$R" | grep -q "OK" && pass "INSERT row 3" || fail "INSERT row 3" "$R"

R=$(query "INSERT+INTO+actors+VALUES+(1,+%27Timothee+Chalamet%27);")
echo "$R" | grep -q "OK" && pass "INSERT into actors" || fail "INSERT into actors" "$R"

R=$(query "INSERT+INTO+nosuchtable+VALUES+(1,+%27x%27,+1);")
echo "$R" | grep -q "ERROR" && pass "INSERT nonexistent table" || fail "INSERT nonexistent table" "$R"

# ── SELECT ──────────────────────────────────────────────
R=$(query "SELECT+*+FROM+movies;")
echo "$R" | grep -q "Lyle" && pass "SELECT * returns data" || fail "SELECT * returns data" "$R"

R=$(query "SELECT+*+FROM+movies;")
COUNT=$(echo "$R" | grep -v "^id" | grep -v "^$" | grep -v "rows" | wc -l | tr -d ' ')
[ "$COUNT" -eq 3 ] && pass "SELECT * returns 3 rows" || fail "SELECT * returns 3 rows (got $COUNT)" "$R"

R=$(query "SELECT+title,length+FROM+movies;")
echo "$R" | grep -q "title,length" && pass "SELECT specific columns" || fail "SELECT specific columns" "$R"

R=$(query "SELECT+*+FROM+movies+WHERE+id+=+3;")
echo "$R" | grep -q "Wicked" && pass "SELECT WHERE id=3" || fail "SELECT WHERE id=3" "$R"

R=$(query "SELECT+*+FROM+movies+WHERE+length+%3C+200;")
echo "$R" | grep -q "Lyle" && pass "SELECT WHERE length<200" || fail "SELECT WHERE length<200" "$R"

R=$(query "SELECT+*+FROM+movies+WHERE+length+%3E+200;")
echo "$R" | grep -q "Unknown" && pass "SELECT WHERE length>200" || fail "SELECT WHERE length>200" "$R"

R=$(query "SELECT+*+FROM+nosuchtable;")
echo "$R" | grep -q "ERROR" && pass "SELECT nonexistent table" || fail "SELECT nonexistent table" "$R"

# ── UPDATE ──────────────────────────────────────────────
R=$(query "UPDATE+movies+SET+length+=+999+WHERE+id+=+2;")
echo "$R" | grep -q "OK" && pass "UPDATE row" || fail "UPDATE row" "$R"

R=$(query "SELECT+*+FROM+movies+WHERE+id+=+2;")
echo "$R" | grep -q "00000999" && pass "UPDATE verified by SELECT" || fail "UPDATE verified by SELECT" "$R"

R=$(query "UPDATE+nosuchtable+SET+length+=+1+WHERE+id+=+1;")
echo "$R" | grep -q "ERROR" && pass "UPDATE nonexistent table" || fail "UPDATE nonexistent table" "$R"

# ── DELETE ──────────────────────────────────────────────
R=$(query "DELETE+FROM+movies+WHERE+id+=+2;")
echo "$R" | grep -q "OK" && pass "DELETE row" || fail "DELETE row" "$R"

R=$(query "SELECT+*+FROM+movies;")
echo "$R" | grep -q "Lyle" && fail "DELETE actually removed row" "$R" || pass "DELETE actually removed row"

COUNT=$(query "SELECT+*+FROM+movies;" | grep -v "^id" | grep -v "^$" | grep -v "rows" | wc -l | tr -d ' ')
[ "$COUNT" -eq 2 ] && pass "DELETE leaves 2 rows" || fail "DELETE leaves 2 rows (got $COUNT)" ""

R=$(query "DELETE+FROM+nosuchtable+WHERE+id+=+1;")
echo "$R" | grep -q "ERROR" && pass "DELETE nonexistent table" || fail "DELETE nonexistent table" "$R"

# ── BLOCK BOUNDARY ──────────────────────────────────────
# Each block holds 252 bytes of payload.
# movies row width = 4 + 30 + 8 = 42 bytes → 6 rows per block.
# Insert 8 rows to force a second block to be allocated.

query "CREATE+TABLE+boundary+(id+smallint,+title+char(30),+length+int);"

for i in 1 2 3 4 5 6 7 8; do
    query "INSERT+INTO+boundary+VALUES+($i,+%27Row+Number+$i%27,+$i);" > /dev/null
done

R=$(query "SELECT+*+FROM+boundary;")
COUNT=$(echo "$R" | grep -v "^id" | grep -v "^$" | grep -v "rows" | wc -l | tr -d ' ')
[ "$COUNT" -eq 8 ] && pass "Block boundary: all 8 rows returned" \
                    || fail "Block boundary: all 8 rows returned (got $COUNT)" "$R"

# Verify the row that landed in the second block is correct
echo "$R" | grep -q "Row Number 7" && pass "Block boundary: row 7 readable" \
                                    || fail "Block boundary: row 7 readable" "$R"

echo "$R" | grep -q "Row Number 8" && pass "Block boundary: row 8 readable" \
                                    || fail "Block boundary: row 8 readable" "$R"

# DELETE from across the boundary and verify count drops
query "DELETE+FROM+boundary+WHERE+id+=+7;" > /dev/null
query "DELETE+FROM+boundary+WHERE+id+=+8;" > /dev/null
R=$(query "SELECT+*+FROM+boundary;")
COUNT=$(echo "$R" | grep -v "^id" | grep -v "^$" | grep -v "rows" | wc -l | tr -d ' ')
[ "$COUNT" -eq 6 ] && pass "Block boundary: delete across boundary" \
                    || fail "Block boundary: delete across boundary (got $COUNT)" "$R"

# ── SUMMARY ─────────────────────────────────────────────
echo "========================="
echo "PASSED: $PASS  FAILED: $FAIL"