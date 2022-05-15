#include "headers.h"

std::map<int, string> message_name_map;

int commands_count = 0;

// RowDescription
map <int ,string> Row_Description = 
{
	{1, "Table OID"},
	{2, "Column index"},
	{3, "Type OID"},
	{4, "Column length"},
	{5, "Type modifier"},
	{6, "Format"}
};

// second int value after initial byte 'R', first int == message len
map <string, int> auth_type = 
{
	{"AuthenticationOk", 0}, // len = 8
	{"AuthenticationKerberosV5", 2},
	{"AuthenticationClearTextPassword", 3},
	{"AuthenticationMD5Password", 5}, // length = 12
	{"AuthenticationSCMCredential", 6},
	{"AuthenticationGSS", 7},
	{"AuthenticationGSSContinue", 8}, // len > 2 int
	{"AuthenticationSSPI", 9},
	{"AuthenticationSASL", 10}, // len = 2 int + SASL mechanism name (string)
	{"AuthenticationSASLContinue", 11}, // len = 2 int + SASL data
	{"AuthenticationSASLFinal", 12} // len = 2 int + SASL outcome "additional data"
};

const char *notice_severity[5] = {"WARNING", "NOTICE", "DEBUG", "INFO", "LOG"};
const char *error_severity[3] = {"ERROR", "FATAL", "PANIC"};
// ErrorResponse and NoticeResponse message fields
map <string, int> EN_fields = 
{
	{"Severity", 0x53},
	{"Severity_new", 0x56},
	{"Code", 0x43},
	{"Message", 0x4d},
	{"Detail", 0x44},
	{"Hint", 0x48},
	{"Position", 0x50},
	{"Internal position", 0x70},
	{"Internal query", 0x71},
	{"Where", 0x57},
	{"Schema name", 0x73},
	{"Table name", 0x74},
	{"Column name", 0x63},
	{"Data type name", 0x64},
	{"Constraint name", 0x6e},
	{"File", 0x46},
	{"Line", 0x4c},
	{"Routine", 0x52}
};

map <string, int> message_type = 
{
	{"Authentication", 0x52},
	{"BackendKeyData", 0x4b},
	{"Bind", 0x42},
	{"BindComplete", 0x32},
	{"Close", 0x43}, // also CommandComplete
	{"CloseComplete", 0x33},
	{"CopyDone", 0x63},
	{"CopyData", 0x64},
	{"CopyFail",  0x66},
	{"CopyInResponse", 0x47},
	{"CopyOutResponse", 0x48}, // also Flush
	{"CopyBothResponse", 0x57},
	{"DataRow", 0x44}, // also Describe
	{"EmptyQueryResponse", 0x49},
	{"ErrorResponse", 0x45}, // also Execute
	{"FunctionCall", 0x46},
	{"FunctionCallResponse", 0x56},
	{"GSSResponse", 0x70}, // also 	PasswordMessage, SASLInitialResponse, SASLResponse
	{"NegotiateProtocolVersion", 0x76},
	{"NoData", 0x6e},
	{"NoticeResponse", 0x4e},
	{"NotificationResponse", 0x41},
	{"ParameterDescription", 0x74},
	{"ParameterStatus", 0x53}, // also Sync (byte1 and size only)
	{"Parse", 0x50},
	{"ParseComplete", 0x31},
	{"PortalSuspended", 0x73},
	{"Query", 0x51},
	{"ReadyForQuery", 0x5a},
	{"RowDescription", 0x54},
	{"Terminate", 0x58}
};

map <string,  int> special_code = {
	{"SSLRequest", 80877103},
	{"StartupMessage", 196608},
	{"GSSENCRequest", 80877104},
	{"CancelRequest", 80877102}
};

void print_hex(unsigned int  *buffer, int size) {
	for (int i = 0; i < size; i++) {
		printf("%02x ", buffer[i]);
	}
	printf("\n\n");
}

int get_int16 (char *buffer, int offset) {
	short tmp[2];
	bzero (tmp, sizeof ( tmp) );
	memcpy( tmp, buffer + offset, 2);
	return __bswap_16 (tmp[0]);
}

int get_int (char *buffer, int offset) {
	unsigned int tmp[4];
	bzero (tmp, sizeof ( tmp) );
	memcpy( tmp, buffer + offset, 4);
	// print debug message
	if (debug_flag == 1)
		print_hex (tmp, 4);
	return __bswap_32 (tmp[0]);
}

int getposition (char *array, int size) {
	for (int i=0; i<size; i++){
		if (array[i] == 0x0){
			return i;
		}
	}
}

int hex_to_int(unsigned int *arr){
	// big-endian
	return ( arr[0] << 24) | ( arr[1] << 16) | ( arr[2] << 8) |  arr[3];
}

// log queries
void write_query_to_file(string parsed_info) {
	ofstream myfile("query.txt", ios::app);
	if ( myfile.is_open() ) {
		myfile.write(parsed_info.c_str(), parsed_info.size());
		myfile.write((char*)"\n", 1);
		myfile.close();
	} else {
		perror("fopen() failed while writing");
	}
}

void add_param_to_parsed_info(string param_name, string &parsed_info, int param) {
	string Format_name = "Text";
	if (param_name.compare("Format") != 0) {
		parsed_info = parsed_info + ", \"" + param_name + "\": \"" + std::to_string(param) + "\"";
		return;
	}	
	else {
		if (param_name.compare("Format") == 0 && param == 1)
			Format_name == "Binary";
		parsed_info = parsed_info + ", \"" + param_name + "\": \"" + Format_name + "(" + std::to_string(param) + ")\"}";
	}
}
void parse_RowDescription(string parsed_info, char* buffer){
	printf("Entered %s\n", parsed_info.c_str());
	// 0, byte1 = 'T'
	// 1, int32 - len
	// 5, int16 - number of rows (can be zero): 00 04

	// 7, string - (column) field name
	// int32 - column oid of the table, otherwise zero
	// int16 - column attr numbe of the column, otherwise zero
	// int32 - oid of the field data type
	// int16 - data type size (pg_type.typlen), negative values denote variable-width types
	// int32 - type modifier (pg_attribute.atttypmod), meaning is type-specific
	// int16 the format code being used for the field: 0 (text) or 1 (binary)
	int offset = 1, i = 0, TMP_SIZE = 100, param32, end = 0;
	short rows = 0, param16;
	char tmp_buf[TMP_SIZE];
	int message_len = get_int(buffer, offset);
	rows = get_int16(buffer, offset + 4);
	if (debug_flag == 1)
		printf("number of rows:%u\n", rows);
	offset = 7;
	if (rows > 0) {
		parsed_info = parsed_info + ": {\"field count\": \"" + std::to_string(rows) + "\" :{\"";
		for (i = 1; i < rows; i++) {
			// get column name
			end = offset + getposition(buffer + offset, message_len - offset);
			if (end > 0) {
				// empty tmp_buf
				memset(tmp_buf, 0, TMP_SIZE);
				// get field data
				strncpy(tmp_buf, buffer + offset, end - offset);
				// add column name to parsed_info
				parsed_info = parsed_info + "Column Name" + "\":\"";
				parsed_info.append(tmp_buf);
				parsed_info = parsed_info + "\"";
				// adjust offset value
				offset = end + 1;
			}
			else {
				perror("parse_RowDescription:getposition() failed");
				break;
			}
			// 32. get table oid
			for (int j = 1; j<4; j++) {
				// table oid
				//printf("i = %d\n", j);
				param32 = get_int(buffer, offset);
				offset +=4;
				add_param_to_parsed_info(Row_Description[2*j-1], parsed_info, param32);
				// 16. get column index
				//if (j != 3)	{
				param16 = get_int16(buffer, offset);
				add_param_to_parsed_info(Row_Description[2*j], parsed_info, (int)param16);
				offset +=2;
				// 32. get type oid
				// 16. get column length
				// 32. get type modifier
				// 16. get format: text, binary
			}
			if (i < rows -1)
				parsed_info = parsed_info + ", {\"";
			//printf("parsed_info: %s\n\n", parsed_info.c_str());
		}
		parsed_info = parsed_info + "}";
	}
	//printf("parsed_info: %s\n", parsed_info.c_str());
	return;
}

void parse_CopyData(string &parsed_info, char* buffer) {
	// and CopyFail
	if (debug_flag == 1)
		printf("Entered %s\n", parsed_info.c_str());
	// 0, Byte1 - 'p'
	// 1, Int32 - len
	// 5, String - password
	int message_len = get_int(buffer, 1);
	int offset = 5;
	if (debug_flag == 1)
		printf("message len: %d\n", message_len);
	char tmp_buf[BUF_SIZE];
	memset(tmp_buf, 0, BUF_SIZE);
	// get string
	// check if not ReadyForQuery
	strncpy(tmp_buf, buffer + offset, message_len - offset);
	if (buffer[0] == 0x63)
	{
		parsed_info = parsed_info + ": {\"data\": \"";
	}  
	else if (buffer[0] == 0x66)
	{
		parsed_info = parsed_info + ": {\"error\": \"";
	}
	if (debug_flag == 1)
		printf("string: %s\n", tmp_buf);
	parsed_info.append(tmp_buf);
	parsed_info = parsed_info + "\"}";
}

void parse_Execute (string &parsed_info, char *buffer) {
	// 0, byte1 - 'E'
	// 1, int32 - len
	// 5, string - name of the portal to execute
	// int32 - maximum number of rows to return, if portal contains a query that returns rows (ignored otherwise). zeros denotes 'no limit'
	int message_len = get_int(buffer, 1);
	int offset = 5, end = -1, max_number_of_rows = -1;
	end = offset + getposition(buffer + offset, message_len - offset);
	char tmp_buf[BUF_SIZE];
	memset(tmp_buf, 0, BUF_SIZE);
	if (end > 0){
		strncpy(tmp_buf, buffer + offset, end - offset);
		// add to parsed_info
		parsed_info = parsed_info + ": {\"portal name\":\"";
		parsed_info.append(tmp_buf);
		// adjust offset value
		offset = end + 1;
	}
	else if (end == 0){
		parsed_info = parsed_info + ": {\"portal name\":\"unnamed";
	}
	else {		
		perror("parse_Execute:getposition() failed");
		return;
	}
	if (offset < message_len - 1){
		max_number_of_rows = get_int(buffer, 1);
		if (max_number_of_rows == 0){
			parsed_info = parsed_info + ", \"max number of rows to return\":\"no limit\"}";
		}
		else if (max_number_of_rows > 0){
			parsed_info = parsed_info + ", \"max number of rows to return\":\"" + std::to_string(max_number_of_rows) +"\"}";
		}
	} else {
		parsed_info = parsed_info + "\"}";
	}
}

void parse_Close (string &parsed_info, char *buffer) {
	// 0, byte1 - 'C'
	// 1, int32 - len
	// 5, byte1 - 'S' - close prepared statement; 'P' to close a portal
	// 6, string - the name of the prepared statement or portal (can be unnamed)
	int message_len = get_int(buffer, 1);
	int offset = 6;
	char tmp_buf[BUF_SIZE];
	memset(tmp_buf, 0, BUF_SIZE);
	if (buffer[5] == 'S') {
		parsed_info = parsed_info + ": {\"what to close\":\"prepared statement\", ";
	}
	else if (buffer[5] == 'P') {
		parsed_info = parsed_info + ": {\"what to close\":\"portal\", ";
	}
	parsed_info = parsed_info + "\"name:";
	// check if name empty
	if (message_len > offset+1) {
		strncpy(tmp_buf, buffer + offset, message_len - offset - 1);
		parsed_info.append(tmp_buf);
	}
	else {
		parsed_info = parsed_info + "unnamed";
	}
	parsed_info = parsed_info + "\"}";
}

void parse_NegotiateProtocolVersion(string &parsed_info, char* buffer) {
	// 0, Byte1 - 'v'
	// 1, Int32 - len
	// 5, Int32 - minnor protocol version supported by server for the major procotol version requested by the client
	// 9,Int32 - number of protocol options not recognized by the server
	// 13,String - the option name
	int message_len = get_int(buffer, 1);
	int minor_version = get_int(buffer, 5);
	int not_rec_proto_options = get_int(buffer, 9);
	int offset = 13, field_flag = FALSE, end = -1;
	char tmp_buf[BUF_SIZE];
	memset(tmp_buf, 0, BUF_SIZE);
	sprintf(tmp_buf, ": {\"minor version\":\"%d\", \"not recognized protocol options number\":\"%d\", \"options\":", minor_version, not_rec_proto_options);
	for (int i=0; i < not_rec_proto_options; i++) 
	{
		end = offset + getposition(buffer + offset, message_len - offset);
		if (end > 0) {
			// empty tmp_buf
			memset(tmp_buf, 0, BUF_SIZE);
			// get field data
			strncpy(tmp_buf, buffer + offset, end - offset);
			// add to parsed_info
			parsed_info = parsed_info + "\"";
			parsed_info.append(tmp_buf);
			// adjust offset value
			offset = end + 1;
			if ((end + 2) != message_len)
				parsed_info = parsed_info + "\", ";
			else
				parsed_info = parsed_info + "\"}}";
		}
		else {
			perror("parse_NegotiateProtocolVersion:getposition() failed");
			break;
		}
	}
}

void parse_PasswordMessage(string &parsed_info, char* buffer) {
	if (debug_flag == 1)
		printf("Entered %s\n", parsed_info.c_str());
	// 0, Byte1 - 'p'
	// 1, Int32 - len
	// 5, String - password
	int message_len = get_int(buffer, 1);
	int offset = 5;
	if (debug_flag == 1)
		printf("message len: %d\n", message_len);
	char tmp_buf[BUF_SIZE];
	memset(tmp_buf, 0, BUF_SIZE);
	// get string
	// check if not ReadyForQuery
	if (buffer[0] != 0x5a)
		strncpy(tmp_buf, buffer + offset, message_len - offset);
	if (debug_flag == 1)
		printf("string: %s\n", tmp_buf);
	switch ((int)buffer[0]) {
		case 0x70: 
			// PasswordMessage
			parsed_info = parsed_info + ": {\"password\":\"";
			break;
		case 0x51:
			// Query
			parsed_info = parsed_info + ": {\"query\":\"";
			break;
		case 0x5a:
			// ReadyForQuery
			parsed_info = parsed_info + ": {\"transaction status indicator\":\"" + buffer[offset];
			break;
		case 0x43:
			// CommandComplete
			parsed_info = parsed_info + ": {\"command tag\":\"";
	}
	parsed_info.append(tmp_buf);
	parsed_info = parsed_info + "\"}";
	if (debug_flag == 1)
		printf("parsed_info = %s\n", parsed_info.c_str());
}

void parse_CancelRequest(string &parsed_info, char* buffer, int offset){
	// 0, Int32 - len(16)
	// 4, Int32 - special code special_code::CancelRequest
	// 8, Int32 - process ID of the target backend
	// 12,Int32 - the secret key for the target backend
	int process_ID = get_int(buffer, offset); // offset = 5 for BackendKeyData, 8 for CancelRequest
	int secret_key = get_int(buffer, offset+4);
	char tmp_buf[100];
	memset(tmp_buf, 0, 100);
	sprintf(tmp_buf, ": {\"process ID\":\"%d\", \"seckey key\":\"%d\"", process_ID,secret_key);
	parsed_info.append(tmp_buf);
	parsed_info = parsed_info + "}";
}

void parse_auth_message(string &parsed_info, char *buffer) {
	// byte1 - 'R'
	// Int32 - len
	// Int32 - auth message_code
	// so on
	parsed_info = "";
	int message_len = get_int(buffer, 1);
	int message_code = get_int(buffer, 5);
	char tmp_buf[100];
	int offset = 10;
	// find field type
	if (debug_flag == 1)
	{
		printf("[parse_auth_message] message_code = %d\n", message_code);
		printf("[parse_auth_message] message_len = %d\n", message_len);
	}
	auto result = std::find_if (
			auth_type.begin(),
			auth_type.end(),
			[message_code](const auto& mo) {return mo.second == message_code; });
	if (debug_flag == 1)
	printf("[parsed_auth_message] message name =%s\n", (result->first).c_str());
	parsed_info = parsed_info + result->first;
	if (message_len == 8){
		return;
	} else {
		parsed_info = parsed_info + ": {";
		if (result->first == "AuthenticationMD5Password"){
			char str[16];
			memset(str, 0, 16);
			sprintf(str, "\"Salt\": \"%x\"", (unsigned int)buffer[10]);
			parsed_info.append(str);
			parsed_info = parsed_info + "}";
			return;
		}
		memset(tmp_buf, 0, 100);
		// get field data
		strncpy(tmp_buf, buffer + offset, message_len - offset-1);
		// add to parsed_info
		switch (result->second){
			// AuthenticationGSSContinue
			case 8:
				parsed_info = parsed_info + "\"GSSAPI or SSPI auth data\": ";
				break;
			// AuthenticationSASL
			case 10:
				parsed_info = parsed_info + "\"SASL auth mechanism name\": ";
			//"AuthenticationSASLContinue
				break;
			case 11:
				parsed_info = parsed_info + "\"SASL data\": ";
				break;
			case 12:
				parsed_info = parsed_info + "\"SASL additional data\": ";
				break;
			default:
				parsed_info = parsed_info + "\"Auth data\": ";
		}
		parsed_info.append(tmp_buf);
		parsed_info = parsed_info + "\"}";
		return;
	}
}

void parse_startup_message (string &parsed_info, char *buffer) {
	// int32 - len
	// int32 - protocol version
	// info: user'\0'user_name and so on
	int offset = 8, end = -1, field_flag = FALSE;
	char tmp_buf [BUF_SIZE];
	int message_len = get_int(buffer, 0);
	if (debug_flag == 1)
	{
		printf("message len = %d\n", message_len);
		printf("Entered parse_startup_message\n");
	}
	parsed_info = parsed_info + ": {";
	do {
		//memset(buffer, 1, offset-1);
		end = offset + getposition(buffer + offset, message_len-offset);
		if (end > 0) {
			// empty tmp_buf
			memset(tmp_buf, 0, BUF_SIZE);
			// get field data
			strncpy(tmp_buf, buffer + offset, end - offset);
			// add to parsed_info
			parsed_info.append(tmp_buf);
			// adjust offset value
			offset = end + 1;
			if (field_flag == FALSE) {
				parsed_info = parsed_info + ":\"";
				field_flag = TRUE;
				} 
			else if (field_flag == TRUE) {
				if ((end + 2) != message_len)
					parsed_info = parsed_info + "\", ";
				else
					parsed_info = parsed_info + "\"}";
				field_flag = FALSE;
			}
		}
		else {
			perror("parse_notice_errorResponse:getposition() failed");
			break;
		}
	} while (offset < message_len - 1); // terminate with '\0'
}

void parse_notice_errorResponse(string &parsed_info, char *buffer) {
	// byte 1 = 'E', 
	// Int32 - length
	// Byte1 - code of field type, if zero - message terminate, else 
	// parse len
	int message_len = get_int(buffer, 1);
	// Get fields for section 53.8
	int offset = 5, x = 0, end = -1;
	parsed_info = parsed_info + ": {";
	char tmp_buf [BUF_SIZE];
	do {
		x = buffer[offset];
		// find field type
		auto result = std::find_if (
					EN_fields.begin(),
					EN_fields.end(),
					[x](const auto& mo) {return mo.second == x; });
		parsed_info = parsed_info + result->first + ":\"";
		// find the end of field data by delimeter '\0'
		end = offset + getposition(buffer + offset, message_len-offset);
		if (end > 0) {
			// empty tmp_buf
			memset(tmp_buf, 0, BUF_SIZE);
			// get field data
			strncpy(tmp_buf, buffer + offset+1, end - offset);
			// add to parsed_info
			parsed_info.append(tmp_buf);
			// adjust offset value
			offset = end + 1;
			if (offset < message_len-1)
				parsed_info = parsed_info + "\", ";
			else 
				parsed_info = parsed_info + "\"}";
		} else {
			perror("parse_notice_errorResponse:getposition() failed");
			break;
		}
	} while (offset < message_len - 1); // terminate with '\0'
	printf("Message parsed: %s\n", parsed_info.c_str());
}

// parse message
void parse_message(string &parsed_info, char *buffer) {
	string message_name = parsed_info;
	if (message_name == "ErrorResponse")
	{
		printf("This is %s message\n", message_name.c_str());
		parse_notice_errorResponse(parsed_info, buffer);
		return;
	}
	if (message_name == "SSLRequest"){
		// no need to parse
		return;
	}
	if (message_name == "StartupMessage"){
		printf("This is %s message\n", message_name.c_str());
		parse_startup_message(parsed_info, buffer);
		return;
	}
	if (message_name == "GSSENCRequest")
		// no need to parse
	if (message_name == "CancelRequest"){
		parse_CancelRequest(parsed_info, buffer, 8);
		return;	
	}
	if (message_name == "Authentication"){
		// message type is undefined - must be identified
		parse_auth_message(parsed_info, buffer);
		return;
	}
	if (message_name == "BackendKeyData")
		// len = 12
		parse_CancelRequest(parsed_info, buffer, 5);
	//if (message_name == "Bind")
			//*/
	if (message_name == "BindComplete")
		// no need to parse
	if (message_name == "Close")
	{
		parse_Close(parsed_info, buffer);
	}
	if (message_name ==  "CommandComplete"){
		parse_PasswordMessage(parsed_info, buffer);
		return;	
	}
	//if (message_name == "CloseComplete")
		// no need to parse
	//if (message_name == "CopyData")
		//*/
	//if (message_name == "CopyDone")
		// no need to parse
	if (message_name == "CopyFail"){
		parse_startup_message (parsed_info, buffer);
		return;
	}
	/*	//
	if (message_name == "CopyInResponse")
		//
	if (message_name == "CopyOutResponse")
		//*/
	//if (message_name == "Flush")
		// no need to parse
		/*
	if (message_name == "CopyBothResponse")
		//
	if (message_name == "DataRow")
		//*/
	if (message_name == "Describe")
	{
		parse_Close(parsed_info, buffer);
		return;
	}
	//if (message_name == "EmptyQueryResponse")
		// no need to parse
	if (message_name == "Execute")
	{
	 	parse_Execute(parsed_info, buffer);
		return;
	}/*
	if (message_name == "FunctionCall")
			//
		if (message_name == "FunctionCallResponse")
			//*/
	if (message_name == "GSSResponse"){
		if (debug_flag == 1)
			printf("commands_count : %d, message_name_map: %s\n", commands_count, (message_name_map[commands_count]).c_str());
		if (commands_count > 0) 
		{
			string last_command = message_name_map[commands_count-1];
			if (last_command == "AuthenticationClearTextPassword" or last_command == "AuthenticationMD5Password")
			{
				parsed_info = "PasswordMessage";
				parse_PasswordMessage(parsed_info, buffer);
				return;
			}
		}
	}
	/*
		if (message_name == "SASLInitialResponse")
			//
		if (message_name == "SASLResponse")
			//*/
	if (message_name == "NegotiateProtocolVersion")
	{
		parse_NegotiateProtocolVersion(parsed_info, buffer);
		return;
	}
	//if (message_name == "NoData")
		// no need to parse
	if (message_name == "NoticeResponse"){
		parse_notice_errorResponse(parsed_info, buffer);
		return;
	}
		/*
		if (message_name == "NotificationResponse")
			//
		if (message_name == "ParameterDescription")
			//
		if (message_name == "ParameterStatus")
			// */
		//if (message_name == "Sync")
			// no need to parse
		/*
		if (message_name == "Parse")
			//
		if (message_name == "ParseComplete")
			//*/
		//if (message_name == "PortalSuspended")
			// no need to parse
		if (message_name == "Query")
		{
			parse_PasswordMessage(parsed_info, buffer);
			return;
		}
		if (message_name == "ReadyForQuery")
		{
			parse_PasswordMessage(parsed_info, buffer);
			return;
		}
	    if (message_name == "RowDescription")
		{
			parse_RowDescription(parsed_info, buffer);
			return;
		}	
			//*/
	//	if (message_name == "Terminate")
			// no need to parse
	// write parsed_info to the queries.txt
}

string check_first_byte(char* buffer, int message_len) {
	string message_name = "";
	string ret  = "";
	int _message_type = 0;
	int x = (int) buffer[0];
	//printf ("First byte: %02x, message len = %d\n", x, message_len);
	if (x == 0) {
		//printf("Eurika! This is special message\n");
		//	may be message which special code
		unsigned int tmp[4];
		bzero (tmp, sizeof ( tmp) );
		memcpy( tmp, buffer + 4, 4);
		// print debug message
		//print_hex (tmp, 4);
		int code = __bswap_32 (tmp[0]);
		//int code = hex_to_int (tmp);
		if (code > 0) {
			if (debug_flag == 1)
				printf("found code: %d\n", code);
		} else{
			if (debug_flag == 1)
				perror("[check_first_byte] hex_to_int() failed");
		}
		// search for code
		auto result_s = std::find_if(
					special_code.begin(),
					special_code.end(),
					[code](const auto& mo) {return mo.second == code; });
		if (result_s != special_code.end() ) {
			// found special_code
			if (debug_flag == 1)
				printf("[success] Found special_code: %s\n", result_s->first.c_str());
			// got message type
			_message_type = result_s->second;
			message_name = result_s->first;
		} else {
			if (debug_flag == 1)
				perror("check_first_byte() failed: undefined message");
			return message_name;
		}
	} else {
		// check command type
		// lambda
		auto result = std::find_if (
					message_type.begin(),
					message_type.end(),
					[x](const auto& mo) {return mo.second == x; });
		if ( result != message_type.end() ) {
			// found message_type
			if (debug_flag == 1)
				printf ("[check_first_byte] Found message_type: %s\n", (result->first).c_str());
			// got message type
			_message_type = result->second;
			message_name = result->first;
		}	else {
			if (debug_flag == 1)
				perror("check_first_byte() failed: undefined message");
			return message_name;
		}
	}
	if (debug_flag == 1)
		printf("Trying to parse message..., message type len = %d\n", (int)message_name.size());
	// found message type - parse response
	ret = message_name;
	if (message_len > 1) {
		//if (message_name.compare(0, 5, "Query") == 0){
			parse_message(message_name, buffer);
			write_query_to_file(message_name);
		//}
	}
	// not need to parse, no data in message
	else {
		//if (message_name.compare(0, 5, "Query") == 0)
		write_query_to_file(message_name);
	}
	if (ret == "Authentication"){
		return message_name.substr(0, message_name.find_first_of(":"));
	}
	return ret;
}
