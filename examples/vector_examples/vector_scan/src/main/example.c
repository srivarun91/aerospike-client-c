/*******************************************************************************
 * Copyright 2008-2025 by Aerospike.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ******************************************************************************/

//==========================================================
// Includes
//

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <aerospike/aerospike.h>
#include <aerospike/aerospike_key.h>
#include <aerospike/aerospike_scan.h>
#include <aerospike/as_error.h>
#include <aerospike/as_key.h>
#include <aerospike/as_record.h>
#include <aerospike/as_status.h>
#include <aerospike/as_vector.h>
#include <aerospike/as_ml_vector.h>

#include "example_utils.h"

//==========================================================
// Constants
//

const char TEST_NAMESPACE[] = "test";
const char TEST_SET[] = "demo";

//==========================================================
// Forward Declarations
//

bool vector_scan_callback(const char* namespace, const uint8_t* digest,
	const char* set, double distance, void* udata);
void cleanup(aerospike* as);
bool insert_test_records(aerospike* as);

//==========================================================
// Vector Scan Example
//

int
main(int argc, char* argv[])
{
	// Parse command line arguments.
	if (! example_get_opts(argc, argv, EXAMPLE_MULTI_KEY_OPTS)) {
		exit(-1);
	}

	// Connect to the aerospike database cluster.
	aerospike as;
	example_connect_to_aerospike(&as);

	// Start clean.
	example_remove_test_records(&as);

	// Insert test records with vector data.
	if (! insert_test_records(&as)) {
		cleanup(&as);
		exit(-1);
	}

	// Create a query vector (4-dimensional float32 vector).
	float query_vector[] = {1.0f, 2.0f, 3.0f, 4.0f};
	as_vector vector;

	if (as_ml_vector_init_float32(&vector, query_vector, 4) != AEROSPIKE_OK) {
		LOG("ERROR: Failed to initialize query vector");
		cleanup(&as);
		exit(-1);
	}

	// Create and configure the scan.
	as_scan scan;
	as_scan_init(&scan, TEST_NAMESPACE, TEST_SET);

	// Set the vector for similarity search.
	as_scan_set_vector(&scan, &vector, AS_ML_VECTOR_FLOAT32, "vector_bin");

	LOG("executing vector scan...");

	// Execute the vector scan.
	as_error err;
	if (aerospike_vector_scan(&as, &err, NULL, &scan, vector_scan_callback, NULL) != AEROSPIKE_OK) {
		LOG("ERROR: aerospike_vector_scan() returned %d - %s", err.code, err.message);
		as_vector_destroy(&vector);
		as_scan_destroy(&scan);
		cleanup(&as);
		exit(-1);
	}

	LOG("vector scan completed");

	// Clean up.
	as_vector_destroy(&vector);
	as_scan_destroy(&scan);
	cleanup(&as);

	LOG("vector scan example successfully completed");
	return 0;
}

//==========================================================
// Vector Scan Callback
//

bool
vector_scan_callback(const char* namespace, const uint8_t* digest,
	const char* set, double distance, void* udata)
{
	// Convert digest to hex string for display.
	char digest_str[41];
	for (int i = 0; i < 20; i++) {
		sprintf(&digest_str[i * 2], "%02x", digest[i]);
	}
	digest_str[40] = '\0';

	LOG("Record found:");
	LOG("  Namespace: %s", namespace);
	LOG("  Set: %s", set ? set : "(null)");
	LOG("  Digest: %s", digest_str);
	LOG("  Distance: %.6f", distance);
	LOG("");

	return true; // Continue processing more records.
}

//==========================================================
// Helpers
//

void
cleanup(aerospike* as)
{
	example_remove_test_records(as);
	example_cleanup(as);
}

bool
insert_test_records(aerospike* as)
{
	// Insert some test records with vector data.
	// In a real application, you would have actual vector data.

	LOG("inserting test records with vector data...");

	// Test vectors (4-dimensional float32).
	float vectors[][4] = {
		{1.1f, 2.1f, 3.1f, 4.1f},  // Similar to query vector
		{5.0f, 6.0f, 7.0f, 8.0f},  // Different from query vector
		{0.9f, 1.9f, 2.9f, 3.9f},  // Very similar to query vector
		{10.0f, 20.0f, 30.0f, 40.0f} // Very different from query vector
	};

	for (int i = 0; i < 4; i++) {
		// Create vector.
		as_vector vector;
		if (as_ml_vector_init_float32(&vector, vectors[i], 4) != AEROSPIKE_OK) {
			LOG("ERROR: Failed to initialize vector %d", i);
			return false;
		}

		// Serialize vector to bytes.
		as_bytes vector_bytes;
		if (as_ml_vector_serialize(&vector, AS_ML_VECTOR_FLOAT32, &vector_bytes) != AEROSPIKE_OK) {
			LOG("ERROR: Failed to serialize vector %d", i);
			as_vector_destroy(&vector);
			return false;
		}

		// Create record.
		as_record rec;
		as_record_inita(&rec, 2);
		as_record_set_int64(&rec, "id", i);
		as_record_set_bytes(&rec, "vector_bin", &vector_bytes);

		// Create key.
		as_key key;
		as_key_init_int64(&key, TEST_NAMESPACE, TEST_SET, i);

		// Insert record.
		as_error err;
		if (aerospike_key_put(as, &err, NULL, &key, &rec) != AEROSPIKE_OK) {
			LOG("ERROR: aerospike_key_put() returned %d - %s", err.code, err.message);
			as_vector_destroy(&vector);
			as_bytes_destroy(&vector_bytes);
			as_record_destroy(&rec);
			as_key_destroy(&key);
			return false;
		}

		// Clean up.
		as_vector_destroy(&vector);
		as_bytes_destroy(&vector_bytes);
		as_record_destroy(&rec);
		as_key_destroy(&key);
	}

	LOG("inserted %d test records", 4);
	return true;
}
