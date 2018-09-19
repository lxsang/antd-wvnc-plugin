void EncodeJPEG(boost::uint8_t *rgb, const int &width, const int &height,
                boost::shared_array<boost::uint8_t> &outbuffer, int *size) {
  jpeg_compress_struct cinfo = {0};  

  jpeg_error_mgr jerror = {0};
  jerror.trace_level = 10;
  cinfo.err = jpeg_std_error(&jerror);
  jerror.trace_level = 10;
  cinfo.err->trace_level = 10;
  jpeg_create_compress(&cinfo);
  
  boost::uint8_t *jpeg_buffer_raw = NULL;
  unsigned long outbuffer_size = 0;
  jpeg_mem_dest(&cinfo, &jpeg_buffer_raw, &outbuffer_size);

  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 100, false);
  jpeg_start_compress(&cinfo, true);

  JSAMPROW row_pointer[1];
  unsigned counter = 0;
  while (cinfo.next_scanline < cinfo.image_height) {
    row_pointer[0] = (JSAMPROW)(&rgb[cinfo.next_scanline * width * 3]);
    unsigned return_code = jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);

  outbuffer.reset(new boost::uint8_t[outbuffer_size]);
  std::memcpy(outbuffer.get(), jpeg_buffer_raw, outbuffer_size);
  *size = outbuffer_size;
  std::free(jpeg_buffer_raw);
}