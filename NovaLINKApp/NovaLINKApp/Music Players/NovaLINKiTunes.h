// This file is part of NovaLINK.
//
// NovaLINK is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 2 of the
// License, or (at your option) any later version.
//
// NovaLINK is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with NovaLINK. If not, see <http://www.gnu.org/licenses/>.

//
//  NovaLINKiTunes.h
//  NovaLINKApp
//
//  Copyright © 2016 Kyle Neideck
//

// Superclass/Protocol Import
#import "NovaLINKMusicPlayer.h"


@interface NovaLINKiTunes : NovaLINKMusicPlayerBase<NovaLINKMusicPlayer>

// The music player ID (see NovaLINKMusicPlayer.h) used by NovaLINKiTunes instances. (Though NovaLINKApp only ever creates one instance of
// NovaLINKiTunes, sharedMusicPlayerID is exposed so iTunes can be set as the default music player.)
+ (NSUUID*) sharedMusicPlayerID;

@end

