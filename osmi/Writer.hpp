#include <sys/stat.h>

#ifndef WRITER_HPP_
#define WRITER_HPP_

#define NO_WIDTH -1

#define USE_TRANSACTIONS true
#define DONT_USE_TRANSACTIONS false

class Writer {

public:
	Writer(const std::string& dirname, const std::string& layer_name, const bool& use_transaction, const OGRwkbGeometryType& geom_type)
	:m_use_transaction(use_transaction),
	 m_layer(nullptr) {

		m_data_source = get_data_source(dirname, layer_name);

		if (!m_data_source) {
			std::cerr << "Creation of data source for layer '" << layer_name << "' failed." << std::endl;
			exit(1);
		}

		OGRSpatialReference spatialref;
		spatialref.SetWellKnownGeogCS("WGS84");

		this->create_layer(m_data_source, layer_name, geom_type);
	}

	virtual ~Writer() {
		if (m_use_transaction) {
			m_layer->CommitTransaction();
		}
		OGRDataSource::DestroyDataSource(m_data_source);
	};

	virtual void feed_node(const osmium::Node&) = 0;

	virtual void feed_way(const osmium::Way&) = 0;

	virtual void feed_relation(const osmium::Relation&) = 0;

protected:
	osmium::geom::OGRFactory<> m_factory {};
	std::string m_layer_name;
	bool m_use_transaction;
	OGRLayer* m_layer;

	struct field_config {
		std::string  name;
		OGRFieldType type;
		int          width;
	};

	void create_fields(const std::vector<field_config>& field_configurations) {
		for (auto it = field_configurations.cbegin(); it!=field_configurations.cend(); ++it) {
			OGRFieldDefn field_defn(it->name.c_str(), it->type);
			if (it->width != NO_WIDTH) {
				field_defn.SetWidth(it->width);
			}
			if (m_layer->CreateField(&field_defn) != OGRERR_NONE) {
				std::cerr << "Creating field '" << it->name <<"' for layer '" << m_layer_name << "' failed." << std::endl;
				exit(1);
			}
		}
		if (m_use_transaction) {
			m_layer->StartTransaction();
		}
	}

	void create_feature(OGRFeature* feature) {
		OGRErr e = m_layer->CreateFeature(feature);
		if (e != OGRERR_NONE) {
			std::cerr << "Failed to create feature. e = " << e << std::endl;
			exit(1);
		}
		OGRFeature::DestroyFeature(feature);
		maybe_commit_transaction();
	}

	void catch_geometry_error(const osmium::geometry_error& e, const osmium::Way& way) {
		std::cerr << "Ignoring illegal geometry for way with id = " << way.id() << " e.what() = " << e.what() << std::endl;
	}

private:
	OGRDataSource* m_data_source;

	unsigned int num_features = 0;

	static bool is_output_dir_written;

	void create_layer(OGRDataSource* data_source, const std::string& layer_name, const OGRwkbGeometryType& geom_type) {
		OGRSpatialReference sparef;
		sparef.SetWellKnownGeogCS("WGS84");

		const char* layer_options[] = { "SPATIAL_INDEX=no", "COMPRESS_GEOM=yes", nullptr };

		this->m_layer = data_source->CreateLayer(layer_name.c_str(), &sparef, geom_type, const_cast<char**>(layer_options));
		if (!m_layer) {
			std::cerr << "Creation of layer '"<< layer_name << "' failed.\n";
			exit(1);
		}
	}

	void maybe_commit_transaction() {
		num_features++;
		if (m_use_transaction && num_features > 10000) {
			m_layer->CommitTransaction();
			m_layer->StartTransaction();
			num_features = 0;
		}
	}

	OGRDataSource* get_data_source(const std::string& dir_name, const std::string& layer_name) {
		const std::string driver_name = std::string("SQLite");
		OGRSFDriver* driver = OGRSFDriverRegistrar::GetRegistrar()->GetDriverByName(driver_name.c_str());
		if (!driver) {
			std::cerr << driver_name << " driver not available." << std::endl;
			exit(1);
		}

		CPLSetConfigOption("OGR_SQLITE_SYNCHRONOUS", "OFF");
		CPLSetConfigOption("OGR_SQLITE_SYNCHRONOUS", "FALSE");
		CPLSetConfigOption("OGR_SQLITE_CACHE", "1024"); // size in MB; see http://gdal.org/ogr/drv_sqlite.html
		const char* options[] = { "SPATIALITE=TRUE", nullptr };

		std::string full_dir = get_current_dir() + "/" + dir_name;

		maybe_create_dir(full_dir);

		std::string path = full_dir + "/" + layer_name + ".sqlite";

		return driver->CreateDataSource(path.c_str(), const_cast<char**>(options));
	}

	std::string get_current_dir() {
		// http://stackoverflow.com/a/145309
		// http://stackoverflow.com/a/13047398
		char* current_path = getcwd(NULL, 0);
		if (current_path == NULL){
			std::cerr << "ERROR: Could not get current directory. errno=" << errno << std::endl;
			exit(1);
		}

		std::string current_path_string(current_path);

		free(current_path);

		return current_path_string;
	}

	void maybe_create_dir(const std::string& dir) {
		if (!is_output_dir_written) {
			is_output_dir_written = true;
			create_dir(dir);
		}
	}

	void create_dir(const std::string& dir) {
		if (mkdir(dir.c_str(), S_IRWXU |  S_IRGRP |  S_IXGRP |  S_IROTH |  S_IXOTH)) { // 755
			std::cerr << "Could not create directory " << dir << ". errno=" << errno << std::endl;
			exit(1);
		}
	}
};

bool Writer::is_output_dir_written = false;

#endif /* WRITER_HPP_ */
