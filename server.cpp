#include <sstream>
#include <iostream>
#include <fstream>
#include <string>
#include <fcntl.h>

// libmicrohttpd
#include <microhttpd.h>

// libcryptopp
//#include <cryptopp/sha.h>
//#include <cryptopp/files.h>
//#include <cryptopp/hex.h>
#include <openssl/sha.h>

// libarchive
#include <archive.h>
#include <archive_entry.h>

#include "utilities.hh"
#include "server.hh"

// to use command line tools
#include <stdlib.h>

using namespace std;

/*
 * Various responses to GET requests
 */

string askpage_head = "<html><body>\n\
                       Upload a Faust file, please.<br>\n\
                       There are ";


string askpage_tail = " clients uploading at the moment.<br>\n\
                       <form action=\"/filepost\" method=\"post\" enctype=\"multipart/form-data\">\n\
                       <input name=\"file\" type=\"file\">\n\
                       <input type=\"submit\" value=\" Send \"></form>\n\
                       </body></html>";


string cannotcompile =
    "<html><body>Could not execute the provided DSP program for the given architecture file.</body></html>";

string nosha1present =
    "<html><body>The given SHA1 key is not present in the directory.</body></html>";

string invalidosorarchitecture =
    "<html><body>You have entered either an invalid operating system, an invalid architecture, or an invalid makefile command.\
    Requests should be of the form:<br/>os/architecture/command<br/>For example:<br/>osx/csound/binary</body></html>";

string invalidinstruction =
    "<html><body>The server only can generate binary, source, or svg for a given architecture.</body></html>";

string busypage =
    "<html><body>This server is busy, please try again later.</body></html>";

string completebuterrorpage =
    "<html><body>The upload has been completed but your Faust file is corrupt. It has not been registered.</body></html>";

string completebutmorethanoneDSPfile =
    "<html><body>The upload has been completed but there is more than one DSP file in your archive. Only one is allowed..</body></html>";

string completebutnoDSPfile =
    "<html><body>The upload has been completed but there is no DSP file in your archive. You must have one and only one.</body></html>";

string completebutdecompressionproblem =
    "<html><body>The upload has been completed but the server could not decompress the archive.</body></html>";

string completebutendoftheworld =
    "<html><body>An internal server error of epic proportions has occurred. This likely portends the end of the world.</body></html>";

string completebutnopipe =
    "<html><body>Could not create a PIPE to faust on the server.</body></html>";

string completebutnohash =
    "<html><body>The upload is completed but we could not generate a hash for your file.</body></html>";

string completebutcorrupt_head =
    "<html><body><p>The upload is completed but the file you uploaded is not a valid Faust file. \
  Make sure that it is either a file with an extension .dsp or an archive (tar.gz, tar.bz, tar \
  or zip) containing one .dsp file and potentially .lib files included by the .dsp file. \
  Furthermore, the code in these files must be valid faust code.</p> \
  <p>Below is the STDOUT and STDERR for the Faust code you tried to compile. \
  If the two are empty, then your file structure is wrong. Otherwise, they will tell you \
  why Faust failed.</p>"                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    ;

string completebutcorrupt_tail =
    "</body></html>";

string completebutalreadythere_head =
    "<html><body>The upload is completed but it looks like you have already uploaded this file.<br />Here is its SHA1 key: ";

string completebutalreadythere_tail =
    "<br />Use this key for all subsequent GET commands.</body></html>";

string completepage_head =
    "<html><body>The upload is completed.<br />Here is its SHA1 key: ";

string completepage_tail =
    "<br />Use this key for all subsequent GET commands.</body></html>";

string errorpage =
    "<html><body>This doesn't seem to be right.</body></html>";

string servererrorpage =
    "<html><body>An internal server error has occured.</body></html>";

string fileexistspage =
    "<html><body>This file already exists.</body></html>";

string debugstub =
    "<html><body>Rien ne s'est cass&eacute; la figure. F&eacute;licitations !</body></html>";

/*
 * Validates that a Faust file or archive is sane and returns 0 for success
 * or 1 for failure.  If the evaluation fails, the appropriate error message
 * is set.  More info on the con_info structure is in server.hh.
 */

int validate_faust(connection_info_struct *con_info)
{
    fs::path tmpdir = fs::temp_directory_path() / fs::unique_path("%%%%-%%%%-%%%%-%%%%");
    fs::create_directory(tmpdir);
    fs::path filename = fs::path(con_info->filename);
    fs::path old_full_filename = fs::path(con_info->tmppath) / filename;

    // libarchive stuff
    struct archive *my_archive;
    struct archive_entry *my_entry;

    my_archive = archive_read_new();
    archive_read_support_filter_all(my_archive);
    archive_read_support_format_all(my_archive);
    int archive_status = archive_read_open_filename(my_archive, old_full_filename.string().c_str(), 10240);

    // prepare for the tar

    if (!fs::is_regular_file(old_full_filename)) {
        fs::remove_all(tmpdir);
        con_info->answerstring = completebuterrorpage;
        return 1;
    } else if (old_full_filename.string().substr(old_full_filename.string().find_last_of(".") + 1) == "dsp") {
        fs::copy_file(old_full_filename, tmpdir / filename);
    } else if (archive_status == ARCHIVE_OK) {
        string dsp_file;
        while (archive_read_next_header(my_archive, &my_entry) == ARCHIVE_OK) {
            fs::path current_file = fs::path(archive_entry_pathname(my_entry));
            if (current_file.string().substr(current_file.string().find_last_of(".") + 1) == "dsp") {
                if (!dsp_file.empty()) {
                    archive_status = archive_read_free(my_archive);
                    fs::remove_all(tmpdir);
                    con_info->answerstring = completebutmorethanoneDSPfile;
                    return 1;
                }
                dsp_file = current_file.string();
                filename = dsp_file;
            }
            string newpath = fs::path(tmpdir / current_file).string();
            archive_entry_set_pathname(my_entry, newpath.c_str());
            archive_read_extract(my_archive, my_entry, ARCHIVE_EXTRACT_PERM);
        }
        archive_status = archive_read_free(my_archive);
        if (archive_status != ARCHIVE_OK) {
            fs::remove_all(tmpdir);
            con_info->answerstring = completebutdecompressionproblem;
            return 1;
        }
    } else {
        fs::remove_all(tmpdir);
        con_info->answerstring = completebutendoftheworld;
        return 1;
    }

    // in case a dsp file wasn't found
    if (filename.string() == "") {
        fs::remove_all(tmpdir);
        con_info->answerstring = completebutnoDSPfile;
        return 1;
    }

    string result = "";
    FILE *pipe = popen(("faust -a plot.cpp " + (tmpdir / filename).string() + " 2>&1").c_str(), "r");
    if (!pipe) {
        con_info->answerstring = completebutnopipe;
    } else {
        // Bleed off the pipe
        char buffer[128];
        while (!feof(pipe)) {
            if (fgets(buffer, 128, pipe) != NULL) {
                result += buffer;
            }
        }
    }

    int exitstatus = pclose(pipe);
    fs::remove_all(tmpdir);

    if (exitstatus) {
        con_info->answerstring = completebutcorrupt_head + result + completebutcorrupt_tail;
    }

    return exitstatus;
}

/*
 * Generates an SHA-1 key for Faust file or archive and returns 0 for success
 * or 1 for failure along with the key in the string_and_exitstatus structure.
 * If the evaluation fails, the appropriate error message is set. More info
 * on the con_info structure is in server.hh.
 */

string_and_exitstatus generate_sha1(connection_info_struct *con_info)
{
    fs::path old_full_filename = fs::path(con_info->tmppath) / fs::path(con_info->filename);
    string source = old_full_filename.string();
    string hash = "";

    ifstream myFile (source.c_str (), ios::in | ios::binary);
    myFile.seekg (0, ios::end);
    int length = myFile.tellg();
    myFile.seekg (0, ios::beg);

    char result[length];
    // read data as a block:
    myFile.read (result, length);
    myFile.close();
    unsigned char obuf[20];
    string setter(result);

    SHA1((const unsigned char *)(setter.c_str()), setter.length(), obuf);
    char buffer[20];
    stringstream ss;
    
    for (int i=0; i < 20; i++) {
       sprintf(buffer, "%02x", obuf[i]);
       ss << buffer;
    }
    
    hash = ss.str();

    string_and_exitstatus res;
    res.exitstatus = hash.length() == 40 ? 0 : 1;
    res.str = hash;
    if (res.exitstatus) {
        con_info->answerstring = completebutnohash;
    }
    return res;
}



/*
 * True if it is a .dsp or a .lib source file 
 */

bool isFaustFile(const fs::path& f)
{

	fs::path x = f.extension();
	bool a = (x==".dsp") || (x==".lib");
	//std::cerr << "isFaustFile(" << f << ") = " << a << std::endl;
	return a;
}

bool isMakefile(const fs::path& f)
{
	return f.stem() == "Makefile";
}


/*
 * Copy all faust source files from src directory to destination directory
 */

void copyFaustFiles(const fs::path& src, const fs::path& dst)
{
	assert(is_directory(src));
	assert(is_directory(dst));
	fs::directory_iterator end_iter;
    for (fs::directory_iterator f_iter(src); f_iter != end_iter; ++f_iter) {
    	if ( isFaustFile(f_iter->path()) ) {
    		//std::cerr << "copying(" << f_iter->path() << ", " << dst/f_iter->path().filename() << ") " << std::endl;
    		copy(f_iter->path(), dst/f_iter->path().filename());
    	}
    }
}



/*
 * Creates an arboreal structure in root with the appropriate makefiles.
 */

void create_file_tree(fs::path sha1path, fs::path makefile_directory)
{
	//std::cerr << "ENTER create_file_tree(" << sha1path << ", " << makefile_directory << ")" << std::endl;
    fs::directory_iterator end_iter;
    for (fs::directory_iterator os_iter(makefile_directory); os_iter != end_iter; ++os_iter) {
    	if (fs::is_directory(os_iter->path())) {
        	//std::cerr << "scanning OS directory " << os_iter->path() << std::endl;
        	string OSname = os_iter->path().filename().string();
        	//create_directory(sha1path/OSname);
            for (fs::directory_iterator makefile_iter(os_iter->path()); makefile_iter != end_iter; ++makefile_iter) {
        		string makefileName = makefile_iter->path().filename().string();
        		if (makefileName.substr(0,9) == "Makefile.") {
        			string archName = makefileName.substr(9);
                	//std::cerr << "scanning makefile " << makefile_iter->path() << ", makefile : " << makefileName << ", architecture : " << archName << std::endl;
                	fs::path dstdir = sha1path/OSname/archName;
                	create_directories(dstdir);
                	copy(makefile_iter->path(), dstdir/"Makefile");
                	copyFaustFiles(sha1path, dstdir);
                }
            }
        }
    }
	//std::cerr << "EXIT create_file_tree()" << std::endl;
}
	
/*
 * Makes an initial directory whose name is the SHA-1 key passed in for
 * a Faust file or archive, returning 0 for success or 1 for failure.
 * If the evaluation fails, the appropriate error message is set.
 * More info on the con_info structure is in server.hh.
 */

// TODO: merge with validate function if possible...
int make_initial_faust_directory(connection_info_struct *con_info, string sha1)
{
    fs::path sha1path = fs::path(con_info->directory) / fs::path(sha1);
    if (! fs::is_directory(sha1path)) {
 		std::cerr << "ENTER make_initial_faust_directory(" << con_info << ", " << sha1 << std::endl;
   
    	// first time we have this file
    	fs::create_directory(sha1path);
    	string filename(con_info->filename);
    	fs::path old_full_filename = fs::path(con_info->tmppath) / filename;

		// libarchive stuff
		struct archive *my_archive;
		struct archive_entry *my_entry;

		my_archive = archive_read_new();
		archive_read_support_filter_all(my_archive);
		archive_read_support_format_all(my_archive);
		int archive_status = archive_read_open_filename(my_archive, old_full_filename.string().c_str(), 10240);
		string result = "";

		if (!fs::is_regular_file(old_full_filename)) {
			con_info->answerstring = completebuterrorpage;
			return 1;
		} else if (filename.substr(filename.find_last_of(".") + 1) == "dsp") {
			fs::copy_file(old_full_filename, sha1path / filename);
		} else if (archive_status == ARCHIVE_OK) {
			string dsp_file;
			while (archive_read_next_header(my_archive, &my_entry) == ARCHIVE_OK) {
				fs::path current_file = fs::path(archive_entry_pathname(my_entry));
				string newpath = fs::path(sha1path / current_file).string();
				archive_entry_set_pathname(my_entry, newpath.c_str());
				archive_read_extract(my_archive, my_entry, ARCHIVE_EXTRACT_PERM);
			}
			archive_status = archive_read_free(my_archive);
			if (archive_status != ARCHIVE_OK) {
				con_info->answerstring = completebutdecompressionproblem;
				return 1;
			}
		} else {
			con_info->answerstring = completebutendoftheworld;
			return 1;
		}

		// copy makefile.none to handle non-architecture-specific targets like mdoc.zip, etc
		fs::copy_file(fs::path(con_info->makefile_directory) / "Makefile.none", sha1path / "Makefile");
		
		// create the architecture-specific folders
		create_file_tree(sha1path, fs::path(con_info->makefile_directory));
		
		std::cerr << "EXIT make_initial_faust_directory(" << con_info << ", " << sha1 << std::endl;
	}
	con_info->answerstring = completepage_head + sha1 + completepage_tail;
	return 0;	
}

/*
 * returns the number of elements in a path
 * an implementation using directory_iterators was leading to an abort trap
 * should investigate further...
 */

int pathsize(fs::path path, int n = 0)
{
    if (path.string()=="/" || path.string()=="." || path.string()=="") {
        return n;
    }

    return pathsize(path.parent_path(), n+1);
}

/*
 * Callback that puts GET parameters in a TArgs.  The TArgs typedef is defined
 * in utilities.hh.
 */

int FaustServer::get_params(void *cls, enum MHD_ValueKind, const char *key, const char *data)
{
    TArgs* args = (TArgs*)cls;
    args->insert(pair<string, string> (string(key), string(data)));
    return MHD_YES;
}

/*
 * Function that sends a response to the MHD_Connection after a POST or GET
 * is effectuated.
 */

int FaustServer::send_page(struct MHD_Connection *connection, const char *page, int length,
                       int status_code, const char * type = 0)
{
    int ret;
    struct MHD_Response *response;

    response =
        MHD_create_response_from_buffer(length, (void*)page,
                                        MHD_RESPMEM_PERSISTENT);
    if (!response) {
        return MHD_NO;
    }

    MHD_add_response_header (response, "Content-Type", type ? type : "text/plain");
    ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);

    return ret;
}

/*
 * Callback called every time a POST request comes in by the postprocessor.
 * To understand more about how postprocessors work, consult the microhttpd
 * documentation.
 */

int FaustServer::iterate_post(void *coninfo_cls, enum MHD_ValueKind kind, const char *key,
                          const char *filename, const char *content_type,
                          const char *transfer_encoding, const char *data, uint64_t off,
                          size_t size)
{
    struct connection_info_struct *con_info = (connection_info_struct*)coninfo_cls;
    FILE *fp;

	std::cerr 	<< "ENTER iterate_post (" 
				<< "kind : " << kind 
				<< ", key : " << key 
				<< ", filename: " << filename 
				<< ", content type: " << content_type
				//<< ", transfer_encoding: " << transfer_encoding
				<< ", data pointer: " << (void*)data
				<< ", size: " << size
				<< ")" << std::endl;
    if (con_info->tmppath.empty()) {
        con_info->filename = filename;
        con_info->tmppath = (fs::temp_directory_path() / fs::unique_path("%%%%-%%%%-%%%%-%%%%")).string();
        fs::create_directory(con_info->tmppath);
    }

    string full_path = (fs::path(con_info->tmppath) / fs::path(con_info->filename)).string();

    con_info->answerstring = servererrorpage;
    con_info->answercode = MHD_HTTP_INTERNAL_SERVER_ERROR;

    if (0 != strcmp(key, "file")) {
    	std::cerr << __LINE__ << " FaustServer::iterate_post" << std::endl;
        return MHD_NO;
    }

    if (!con_info->fp) {
        if (NULL != (fp = fopen(full_path.c_str(), "rb"))) {
            fclose(fp);
            con_info->answerstring = fileexistspage;
            con_info->answercode = MHD_HTTP_FORBIDDEN;
    		std::cerr << __LINE__ << " FaustServer::iterate_post" << std::endl;
            return MHD_NO;
        }

        con_info->fp = fopen(full_path.c_str(), "ab");
        if (!con_info->fp) {
    		std::cerr << __LINE__ << " FaustServer::iterate_post" << std::endl;
            return MHD_NO;
        }
    }

    if (size > 0) {
        if (!fwrite(data, size, sizeof(char), con_info->fp)) {
    		std::cerr << __LINE__ << " FaustServer::iterate_post" << std::endl;
            return MHD_NO;
        }
    }

    con_info->answercode = MHD_HTTP_OK;

    return MHD_YES;
}

/*
 * Callback called every time a GET or POST request is completed.
 * Note that this is NOT necessarily called once the entirety of
 * POST data is transfered. If the data is transfered in chunks,
 * this is called after every chunk.
 */

void FaustServer::request_completed(void *cls, struct MHD_Connection *connection,
                               void **con_cls, enum MHD_RequestTerminationCode toe)
{
    std::cerr << "FaustServer::request_completed()" << endl;
    
    struct connection_info_struct *con_info = (connection_info_struct*)*con_cls;

    if (NULL == con_info) {
        return;
    }

    if (con_info->connectiontype == POST) {
        if (NULL != con_info->postprocessor) {
            MHD_destroy_post_processor(con_info->postprocessor);
            nr_of_uploading_clients--;
        }
        if (con_info->fp) {
            fclose(con_info->fp);
        }
    }

    free(con_info);
    *con_cls = NULL;
}

/*
 * Callback called every time a GET or POST request is received.
 * by the server.
 */

int FaustServer::answer_to_connection(void *cls, struct MHD_Connection *connection,
                                  const char *url, const char *method,
                                  const char *version, const char *upload_data,
                                  size_t *upload_data_size, void **con_cls)
{
    std::cerr << "FaustServer::answer_to_connection(" << url << ", " << method  << ", " << version << ")" << std::endl;
    FaustServer *server = (FaustServer*)cls;

    if (NULL == *con_cls) {
        struct connection_info_struct *con_info;

        if (nr_of_uploading_clients >= server->getMaxClients()) {
            return send_page(connection, busypage.c_str (), busypage.size (), MHD_HTTP_SERVICE_UNAVAILABLE, "text/html");
        }

        con_info = new connection_info_struct();
        con_info->directory = server->getDirectory().string();
        con_info->makefile_directory = server->getMakefileDirectory().string();

        if (NULL == con_info) {
            return MHD_NO;
        }

        con_info->fp = NULL;

        if (0 == strcmp(method, "POST")) {
            con_info->postprocessor =
                MHD_create_post_processor(connection, POSTBUFFERSIZE,
                                          iterate_post, (void*)con_info);

            if (NULL == con_info->postprocessor) {
                free(con_info);
                return MHD_NO;
            }

            nr_of_uploading_clients++;

            con_info->connectiontype = POST;
            con_info->answercode = MHD_HTTP_OK;
            con_info->answerstring = errorpage;
        } else {
            con_info->connectiontype = GET;
        }

        *con_cls = (void*)con_info;

        return MHD_YES;
    }

    if (0 == strcmp(method, "GET")) {
        TArgs args;
        MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND, get_params, &args);
        if (!args.size() && strcmp(url, "/") == 0) {
            stringstream ss;
            ss << askpage_head << nr_of_uploading_clients << askpage_tail;
            return send_page(connection, ss.str().c_str (), ss.str().size(), MHD_HTTP_OK, "text/html");
        }
        struct connection_info_struct *con_info = (connection_info_struct*)*con_cls;
        std::cerr << "server->getDirectory() = " << server->getDirectory() << std::endl;
        return faustGet(connection, con_info, url, args, server->getDirectory());
    }

    if (0 == strcmp(method, "POST")) {
    	std::cerr << "POST processing" << std::endl;
        struct connection_info_struct *con_info = (connection_info_struct*)*con_cls;

        if (0 != *upload_data_size) {
            MHD_post_process(con_info->postprocessor, upload_data,
                             *upload_data_size);
            *upload_data_size = 0;

            return MHD_YES;
        } else {
            // need to close the file before request_completed
            // so that it can be opened by the methods below
            if (con_info->fp) {
                fclose(con_info->fp);
            }

            if (!validate_faust(con_info)) {
                string_and_exitstatus sha1 = generate_sha1(con_info);
                if (!sha1.exitstatus) {
                    (void)make_initial_faust_directory(con_info, sha1.str);
                }
            }
            return send_page(connection, con_info->answerstring.c_str(),
                             con_info->answerstring.size(), con_info->answercode, "text/html");
        }
    }

    return send_page(connection, errorpage.c_str(), errorpage.size(), MHD_HTTP_BAD_REQUEST, "text/html");
}

// Number of clients currently uploading

unsigned int FaustServer::nr_of_uploading_clients = 0;

/*
 * Function that handles all get requests, sending back the
 * appropriate response.
 *
 * All the components in the URL represent a file structure
 * created when the file was uploaded. The last component of
 * the url represents an instruction to the Makefile.
 *
 * Any GET parameters are set as environmetnal variables before
 * calling the makefile.
 */
 
// Define here the various targets accepted by faustweb makefiles
bool isValidTarget(const fs::path& target) 
{
	return (target == "binary.zip")
		|| (target == "src.cpp")
		|| (target == "svg.zip")
		|| (target == "mdoc.zip");
}

fs::path make (const fs::path& dir, const fs::path& target)
{
	std::stringstream ss;
	ss << "make -C " << dir << " " << target;
	std::cerr << ss.str() << std::endl;
	if ( 0 == system(ss.str().c_str()) ) {
       	return dir/target;
	} else {
       	std::cerr << __LINE__  << "makefile failed !!!" << std::endl; 
	}
}


/*
 * Function that sends a file in response to a GET
 */

int FaustServer::send_file(struct MHD_Connection *connection, const fs::path& filepath)
{
	struct stat sbuf;
	int 		fd;
		
	std::cerr << __LINE__ << " ENTER send_file : " << filepath << endl;
	
	if ( (-1 == (fd = open (filepath.string().c_str(), O_RDONLY))) || (0 != fstat (fd, &sbuf)) ) {
		std::cerr << __LINE__  << " error accessing file : " << filepath << endl;
		return send_page(connection, cannotcompile.c_str(), cannotcompile.size(), MHD_HTTP_BAD_REQUEST, "text/html");
	}

    struct MHD_Response* response = MHD_create_response_from_fd (sbuf.st_size, fd);
    if (!response) { 
		std::cerr << __LINE__ << " error can't create response : " << filepath << endl;
    	return MHD_NO; 
    }

    MHD_add_response_header (response, "Content-Type", "application/zip");
    MHD_add_response_header (response, "Content-Location", filepath.filename().string().c_str());
    int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);

	std::cerr << __LINE__ << " EXIT send_file : " << filepath << endl;
    return ret;
}




/*
 * Handle a GET command by "making" the appropriate resource and returning it
 */

int FaustServer::faustGet(struct MHD_Connection* connection, connection_info_struct* con_info, const char* raw_url, TArgs &args, const fs::path& directory)
{
    fs::path url 		= fs::path(raw_url);
	fs::path fulldir  	= directory / url.parent_path();
	fs::path target  	= url.filename();
	fs::path makefile 	= fulldir / "Makefile";
	
	if ( fs::is_regular_file(makefile) && isValidTarget(target) ) {
	
		fs::path filename = make (fulldir, target);
		
    	if (!fs::is_regular_file(filename)) {
        	return send_page(connection, cannotcompile.c_str(), cannotcompile.size(), MHD_HTTP_BAD_REQUEST, "text/html");
    	} else {
    		return send_file(connection, filename);
    	}
		
	} else { 
		return send_page(connection, invalidinstruction.c_str(), invalidinstruction.size(), MHD_HTTP_BAD_REQUEST, "text/html");
	}
}

// Get the maximum number of clients allowed to connect at a given time.

const int FaustServer::getMaxClients()
{
    return max_clients_;
}

// Get the directory to which the uploaded files are being written.

fs::path FaustServer::getDirectory()
{
    return directory_;
}

// Get the directory that the makefiles should be copied from.

fs::path FaustServer::getMakefileDirectory()
{
    return makefile_directory_;
}

// Get the path to the logfile.

fs::path FaustServer::getLogfile()
{
    return logfile_;
}

// Start the Faust server - shallow wrapper around MHD_start_daemon

bool FaustServer::start()
{
    daemon_ = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, port_, NULL, NULL,
                               &answer_to_connection, this,
                               MHD_OPTION_NOTIFY_COMPLETED, request_completed,
                               NULL, MHD_OPTION_END);
    return daemon_ != NULL;
}

// Stop the Faust server - shallow wrapper around MHD_stop_daemon

void FaustServer::stop()
{
    if (daemon_) {
        MHD_stop_daemon(daemon_);
    }

    daemon_ = 0;
}


// Constructor for the Faust server

FaustServer::FaustServer (int port, int max_clients, const fs::path&  directory, const fs::path&  makefile_directory, const fs::path&  logfile)
    : port_(port), max_clients_(max_clients), directory_(directory), makefile_directory_(makefile_directory), logfile_(logfile)
{
    std::cerr << "FaustServer::FaustServer(" << port << "," << max_clients << "," << directory << "," << makefile_directory << "," << logfile << ")" << std::endl;
}