/*
    This file is part of darktable,
    copyright (c) 2009--2010 Henrik Andersson.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/darktable.h"
#include "common/image.h"
#include "common/image_cache.h"
#include "common/imageio_module.h"
#include "common/imageio.h"
#include "common/variables.h"
#include "control/control.h"
#include "control/conf.h"
#include "gui/gtk.h"
#include "dtgtk/button.h"
#include "dtgtk/paint.h"
#include <stdio.h>
#include <stdlib.h>
#include <glade/glade.h>

DT_MODULE(1)

typedef struct _email_attachment_t
{
  uint32_t imgid;     // The image id of exported image
  gchar *file;            // Full filename of exported image
}
_email_attachment_t;

// saved params
typedef struct dt_imageio_email_t
{
  char filename[1024];
  GList *images;
}
dt_imageio_email_t;


const char*
name ()
{
  return _("send as email");
}

int recommended_dimension(struct dt_imageio_module_storage_t *self, uint32_t *width, uint32_t *height) {
  *width=1280;
  *height=1280;
  return 1;
}


void
gui_init (dt_imageio_module_storage_t *self)
{
 
}

void
gui_cleanup (dt_imageio_module_storage_t *self)
{
  free(self->gui_data);
}

void
gui_reset (dt_imageio_module_storage_t *self)
{
  
}

int
store (dt_imageio_module_data_t *sdata, const int imgid, dt_imageio_module_format_t *format, dt_imageio_module_data_t *fdata, const int num, const int total)
{
  
  dt_image_t *img = dt_image_cache_use(imgid, 'r');
  dt_imageio_email_t *d = (dt_imageio_email_t *)sdata;

  _email_attachment_t *attachment = ( _email_attachment_t *)malloc(sizeof(_email_attachment_t));
  attachment->imgid = imgid;
   
  char dirname[4096];
  dt_image_full_path(img, dirname, 1024);
  const gchar * filename = g_basename( dirname );
  strcpy( g_strrstr( filename,".")+1, format->extension(fdata));
  
  attachment->file = g_build_filename( g_get_tmp_dir(), filename,NULL );
  
  dt_imageio_export(img, attachment->file, format, fdata);
  dt_image_cache_release(img, 'r');
  
  char *trunc = attachment->file + strlen(attachment->file) - 32;
  if(trunc < attachment->file) trunc = attachment->file;
  dt_control_log(_("%d/%d exported to `%s%s'"), num, total, trunc != filename ? ".." : "", trunc);
 
  d->images = g_list_append( d->images, attachment );
 
  return 0;
}

void*
get_params(dt_imageio_module_storage_t *self)
{
  dt_imageio_email_t *d = (dt_imageio_email_t *)g_malloc(sizeof(dt_imageio_email_t));
  memset( d,0,sizeof( dt_imageio_email_t));
  return d;
}


void
free_params(dt_imageio_module_storage_t *self, void *params)
{
  dt_imageio_email_t *d = (dt_imageio_email_t *)params;
  
  // All images are exported, generate a mailto uri and startup default mail client
  gchar uri[4096]={0};
  gchar body[4096]={0};
  gchar attachments[4096]={0};
  
  gchar *uriFormat=NULL;
  gchar *subject="images exported from darktable";
  gchar *imageBodyFormat="%s %s\n"; // filename, exif oneliner
  gchar *attachmentFormat=NULL;
  gchar *attachmentSeparator="";
  
#ifdef HAVE_GCONF
  gchar *defaultHandler = gconf_client_get_string (darktable.conf->gconf, "/desktop/gnome/url-handlers/mailto/command", NULL);
  if( defaultHandler == NULL ) goto default_handler;
  
  // Ok we got default command for mail let's handle cases..
  if( g_strrstr(defaultHandler,"thunderbird") ) {
      uriFormat="thunderbird -compose \"to='',subject='%s',body='%s',attachment='%s'\"";   // subject, body, and list of attachments with format "<filename>,"
      attachmentFormat="%s";
      attachmentSeparator=",";
    goto proceed;
  } else if( g_strrstr(defaultHandler,"kmail") ) {
      // When I enter the mailto:... in konqueror everything is ok, yet from dt we have no attachements. WTF?
      // So we launch it directly.
      uriFormat="kmail --composer --subject \"%s\" --body \"%s\" --attach \"%s\"";   // subject, body, and list of attachments with format "--attach <filename> "
      attachmentFormat="%s";
      attachmentSeparator="\" --attach \"";
    goto proceed;
  }
  
default_handler: ;    // default handler using standard mailto: format...
#endif
  uriFormat="mailto:?subject=%s&body=%s%s";   // subject, body, and list of attachments with format &attachment=<filename>
  attachmentFormat="&attachment=file://%s";

#ifdef HAVE_GCONF
proceed: ; // Let's build up uri / command
#endif
  while( d->images ) {
    gchar exif[256]={0};
    _email_attachment_t *attachment=( _email_attachment_t *)d->images->data;
    const gchar *filename = g_basename( attachment->file );
    //void dt_image_print_exif(dt_image_t *img, char *line, int len);
    dt_image_t *img = dt_image_cache_use( attachment->imgid, 'r');
    dt_image_print_exif( img, exif, 256 );
    g_snprintf(body+strlen(body),4096-strlen(body), imageBodyFormat, filename, exif );
    
    if( strlen( attachments ) )
      g_snprintf(attachments+strlen(attachments),4096-strlen(attachments), "%s", attachmentSeparator );
    
    g_snprintf(attachments+strlen(attachments),4096-strlen(attachments), attachmentFormat, attachment->file );
  
    // Free attachment item and remove
    dt_image_cache_release(img, 'r');
    g_free( d->images->data );
    d->images = g_list_remove( d->images, d->images->data );
  }

  // build uri and launch before we quit...
  g_snprintf( uri, 4096,  uriFormat, subject, body, attachments );
  //fprintf(stderr,"\n%s\n", uri );
  
  // So what should we do...
  int res=0;
  if( strncmp( uri, "mailto:", 7) == 0 )
    gtk_show_uri(NULL,uri,GDK_CURRENT_TIME,NULL);
  else // Launch subprocess
    res = system( uri );
 
  free(params);
}

