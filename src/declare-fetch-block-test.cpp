/*
 * Test what happens when using block cursors, scrolling next and prior, or
 * fetching a row "behind" the rowset by "fetch absolute".
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "common.h"

#define	TOTAL	120
#define	BLOCK	84

TEST_CASE("declare-fetch-block-test", "[odbc]") {
	test_printf_reset();

	int			rc;
	HSTMT		hstmt = SQL_NULL_HSTMT;
	int		i;
	int		count = TOTAL;
	char		query[100];
	int		totalRows = 0;
	int		fetchIdx = 0;
	SQLLEN		rowArraySize = BLOCK;
	SQLULEN		rowsFetched;
	SQLINTEGER	id[TOTAL];
	SQLLEN		cbLen[TOTAL];

	/****
     * Run this test with UseDeclareFetch = 1 and Fetch=100(default).
     */
	test_connect_ext("UseDeclareFetch=1");

	rc = SQLAllocHandle(SQL_HANDLE_STMT, conn, &hstmt);
	if (!SQL_SUCCEEDED(rc))
	{
		print_diag("failed to allocate stmt handle", SQL_HANDLE_DBC, conn);
		REQUIRE(1==0);
	}

	rc = SQLExecDirect(hstmt, (SQLCHAR *) "create temporary table tmptable(id int4 primary key)", SQL_NTS);
	CHECK_STMT_RESULT(rc, "SQLExecDirect create table failed", hstmt);

	/* insert into a table */
	for (i = 0; i < count; i++)
	{
		snprintf(query, sizeof(query), "insert into tmptable values (%d)", i);
		rc = SQLExecDirect(hstmt, (SQLCHAR *) query, SQL_NTS);
		CHECK_STMT_RESULT(rc, "insert into table failed", hstmt);
	}
	rc = SQLFreeStmt(hstmt, SQL_CLOSE);
	CHECK_STMT_RESULT(rc, "SQLFreeStmt failed", hstmt);

	/*
	 * Block cursor
	 */
	rc = SQLSetStmtAttr(hstmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER) rowArraySize, 0);
	CHECK_STMT_RESULT(rc, "SQLSetStmtAttr ROW_ARRAY_SIZE failed", hstmt);
	rc = SQLSetStmtAttr(hstmt, SQL_ATTR_ROWS_FETCHED_PTR, (SQLPOINTER) &rowsFetched, 0);
	CHECK_STMT_RESULT(rc, "SQLSetStmtAttr ROWS_FETCHED_PTR failed", hstmt);

	auto cb_len_addr = &cbLen;
	rc = SQLBindCol(hstmt, 1, SQL_C_SLONG, &id, 0, cbLen);
	CHECK_STMT_RESULT(rc, "SQLBindCol failed", hstmt);

	rc = SQLExecDirect(hstmt, (SQLCHAR *) "select * from tmptable", SQL_NTS);
	CHECK_STMT_RESULT(rc, "select failed", hstmt);
	while (rc = SQLFetch(hstmt), SQL_SUCCEEDED(rc))
	{
		fetchIdx++;
		totalRows += (int) rowsFetched;
		test_printf("fetchIdx=%d, fetched rows=%d, total rows=%d\n", fetchIdx, (int) rowsFetched, totalRows);
	}
	rc = SQLFreeStmt(hstmt, SQL_CLOSE);
	CHECK_STMT_RESULT(rc, "SQLFreeStmt failed", hstmt);
	/*
	 * Scroll next -> EOF -> prior -> BOF -> next -> EOF -> prior -> BOF
	 */
	rc = SQLSetStmtAttr(hstmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER) 1, SQL_IS_UINTEGER);
	CHECK_STMT_RESULT(rc, "SQLSetStmtAttr ROW_ARRAY_SIZE failed", hstmt);
	rc = SQLSetStmtAttr(hstmt, SQL_ATTR_CURSOR_TYPE, (SQLPOINTER) SQL_CURSOR_STATIC, 0);
	CHECK_STMT_RESULT(rc, "SQLSetStmtAttr CURSOR_TYPE failed", hstmt);
	rc = SQLSetStmtAttr(hstmt, SQL_ATTR_CONCURRENCY, (SQLPOINTER) SQL_CONCUR_ROWVER, 0);
	CHECK_STMT_RESULT(rc, "SQLSetStmtAttr CONCURRENCY failed", hstmt);
	rc = SQLExecDirect(hstmt, (SQLCHAR *) "select * from tmptable", SQL_NTS);
	CHECK_STMT_RESULT(rc, "select failed", hstmt);
	for (i = 0; i < 2; i++)
	{
		totalRows = 0;
		while (rc = SQLFetchScroll(hstmt, SQL_FETCH_NEXT, 0), SQL_SUCCEEDED(rc))
			totalRows += (int) rowsFetched;
		if (SQL_NO_DATA != rc)
			CHECK_STMT_RESULT(rc, "fetch failed", hstmt);
		test_printf("next  total rows=%d\n", totalRows);
		totalRows = 0;
		while (rc = SQLFetchScroll(hstmt, SQL_FETCH_PRIOR, 0), SQL_SUCCEEDED(rc))
			totalRows += (int) rowsFetched;
		if (SQL_NO_DATA != rc)
			CHECK_STMT_RESULT(rc, "fetch failed", hstmt);
		test_printf("prior total rows=%d\n", totalRows);
	}
	rc = SQLFreeStmt(hstmt, SQL_CLOSE);
	CHECK_STMT_RESULT(rc, "SQLFreeStmt failed", hstmt);
	/*
	 * When fetching a row "behind" the rowset by "fetch absolute" only the first ones of the result set can be fetched?
	 */
	rc = SQLExecDirect(hstmt, (SQLCHAR *) "select * from tmptable", SQL_NTS);
	CHECK_STMT_RESULT(rc, "select failed", hstmt);
	if (rc = SQLFetchScroll(hstmt, SQL_FETCH_ABSOLUTE, count + 1), !SQL_SUCCEEDED(rc))
	{
		test_printf("FetchScroll beyond the end failed %d\n", rc);
	}
	rc = SQLFetchScroll(hstmt, SQL_FETCH_ABSOLUTE, 1);
	CHECK_STMT_RESULT(rc, "FetchScroll the 1st row failed", hstmt);
	for (i = 1; i < count; i++)
	{
		if (!SQL_SUCCEEDED(rc = SQLFetchScroll(hstmt, SQL_FETCH_NEXT, 0)))
			break;
	}
	test_printf("encountered EOF at %d\n", i);

	rc = SQLFreeStmt(hstmt, SQL_CLOSE);
	CHECK_STMT_RESULT(rc, "SQLFreeStmt failed", hstmt);

	// Test with more chunks and huge rowset
	const static SQLULEN TOTAL_ROWS = 4096;
	const static SQLULEN ROWSET_SIZE = 512;

	test_printf("\n---Total rows: %d and rowset size: %d\n", TOTAL_ROWS, ROWSET_SIZE);

	/*
	 * Scroll next -> EOF -> prior -> BOF -> next -> EOF -> prior -> BOF
	 */
	rc = SQLSetStmtAttr(hstmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER) 1, SQL_IS_UINTEGER);
	CHECK_STMT_RESULT(rc, "SQLSetStmtAttr ROW_ARRAY_SIZE failed", hstmt);
	rc = SQLSetStmtAttr(hstmt, SQL_ATTR_CURSOR_TYPE, (SQLPOINTER) SQL_CURSOR_STATIC, 0);
	CHECK_STMT_RESULT(rc, "SQLSetStmtAttr CURSOR_TYPE failed", hstmt);
	rc = SQLSetStmtAttr(hstmt, SQL_ATTR_CONCURRENCY, (SQLPOINTER) SQL_CONCUR_ROWVER, 0);
	CHECK_STMT_RESULT(rc, "SQLSetStmtAttr CONCURRENCY failed", hstmt);

	rc = SQLSetStmtAttr(hstmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER) ROWSET_SIZE, 0);
	CHECK_STMT_RESULT(rc, "SQLSetStmtAttr ROW_ARRAY_SIZE failed", hstmt);

	rc = SQLExecDirect(hstmt, (SQLCHAR *) "SELECT * FROM RANGE(4096)", SQL_NTS);
	CHECK_STMT_RESULT(rc, "select failed", hstmt);

	fetchIdx = totalRows =0;
	while (rc = SQLFetch(hstmt), SQL_SUCCEEDED(rc))
	{
		fetchIdx++;
		totalRows += (int) rowsFetched;
		test_printf("fetchIdx=%d, fetched rows=%d, total rows=%d\n", fetchIdx, (int) rowsFetched, totalRows);
	}

	// returning to BOF
	rc = SQLFetchScroll(hstmt, SQL_FETCH_ABSOLUTE, 1);
	CHECK_STMT_RESULT(rc, "FetchScroll the 1st row failed", hstmt);

	for (i = 0; i < 2; i++)
	{
		totalRows = 0;
		while (rc = SQLFetchScroll(hstmt, SQL_FETCH_NEXT, 0), SQL_SUCCEEDED(rc))
			totalRows += (int) rowsFetched;
		if (SQL_NO_DATA != rc)
			CHECK_STMT_RESULT(rc, "fetch failed", hstmt);
		test_printf("next  total rows=%d\n", totalRows);
		totalRows = 0;
		while (rc = SQLFetchScroll(hstmt, SQL_FETCH_PRIOR, 0), SQL_SUCCEEDED(rc))
			totalRows += (int) rowsFetched;
		if (SQL_NO_DATA != rc)
			CHECK_STMT_RESULT(rc, "fetch failed", hstmt);
		test_printf("prior total rows=%d\n", totalRows);
	}

	rc = SQLFreeStmt(hstmt, SQL_CLOSE);
	CHECK_STMT_RESULT(rc, "SQLFreeStmt failed", hstmt);

	// clean up statement
	release_statement(hstmt);

	/* Clean up */
	test_disconnect();

    test_check_result("declare-fetch-block");

	return;
}
